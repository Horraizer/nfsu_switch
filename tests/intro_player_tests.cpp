#include "nfsu/intro_movie.h"
#include "nfsu/audio_output.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
namespace {
unsigned starts=0,stops=0;
std::vector<std::int16_t> rendered;
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
}
// Deterministic zero-latency device. It exercises the very same player and
// resampler as the production outputs, without requiring a desktop speaker.
namespace nfsu::audio {
struct Output::Impl {Render render;mutable std::uint64_t clock=0;bool active=false;mutable std::string error;};
Output::Output():impl_(std::make_unique<Impl>()){}
Output::~Output(){stop();}
bool Output::start(Render render,std::string&) {impl_->render=std::move(render);impl_->active=true;++starts;return true;}
void Output::stop(){if(impl_->active){impl_->active=false;++stops;}}
std::string Output::error() const{return impl_->error;}
Output::Stats Output::stats() const noexcept {
    std::array<std::int16_t,200> pcm{};
    try {impl_->render(pcm.data(),100);rendered.insert(rendered.end(),pcm.begin(),pcm.end());}
    catch(const std::exception& e){impl_->error=e.what();}
    impl_->clock+=100;Stats s;s.played_frames=impl_->clock;return s;
}
}
namespace {
using Bytes=std::vector<std::uint8_t>;
void u32(Bytes& b,unsigned v){for(unsigned i=0;i<4;++i)b.push_back(static_cast<std::uint8_t>(v>>(8*i)));}
void append(Bytes& b,const char* tag,const Bytes& payload){b.insert(b.end(),tag,tag+4);u32(b,unsigned(payload.size()+8));b.insert(b.end(),payload.begin(),payload.end());}
Bytes video(int dc=0) {
    Bytes h(16,0);h[6]=33;h[8]=h[10]=16;h[13]=1;
    std::vector<unsigned> bits;
    for(unsigned j=0;j<6;++j){for(int i=7;i>=0;--i)bits.push_back((unsigned(dc)>>i)&1);bits.push_back(1);bits.push_back(0);}
    while(bits.size()%16)bits.push_back(0);
    Bytes payload(bits.size()/8,0);
    for(std::size_t i=0;i<bits.size();++i)payload[(i/8)^1U]|=static_cast<std::uint8_t>(bits[i]<<(7-i%8));
    h.insert(h.end(),payload.begin(),payload.end());return h;
}
Bytes header(unsigned rate=44100,unsigned frames=28) {
    return {'P','T',0,0,253,128,1,2,130,1,2,132,3,
        static_cast<std::uint8_t>(rate>>16),static_cast<std::uint8_t>(rate>>8),static_cast<std::uint8_t>(rate),
        133,3,static_cast<std::uint8_t>(frames>>16),static_cast<std::uint8_t>(frames>>8),static_cast<std::uint8_t>(frames),255};
}
Bytes sound() {
    Bytes b;u32(b,28);u32(b,0);u32(b,61);
    for(unsigned ch=0;ch<2;++ch){b.insert(b.end(),{238,0,0,0,0});
        for(int i=0;i<28;++i){const auto s=static_cast<unsigned short>((ch?-1:1)*i*100);b.push_back(static_cast<std::uint8_t>(s>>8));b.push_back(static_cast<std::uint8_t>(s));}}
    return b;
}
Bytes fixture(int dc=0){Bytes b;append(b,"MADk",video(dc));append(b,"SCHl",header());append(b,"SCDl",sound());append(b,"SCEl",{});return b;}
void write(const std::filesystem::path& p,const Bytes& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));require(bool(f),"fixture write");}
void rejects(const std::filesystem::path& path,const Bytes& b,bool audio=false) {
    write(path,b);bool failed=false;
    try {nfsu::MovieReader m(path.string());nfsu::MovieFrame f;
        if(audio){std::vector<std::int16_t> pcm;while(m.next_audio(pcm)){};}else{while(m.next_video(f)){};}}
    catch(const std::exception&){failed=true;}
    require(failed,"corrupt movie accepted");
}
}
int main() {
    const auto stamp=std::chrono::high_resolution_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const auto pid=_getpid();
#else
    const auto pid=getpid();
#endif
    const auto dir=std::filesystem::temp_directory_path()/("nfsu-intro-"+std::to_string(pid)+"-"+std::to_string(stamp));
    try {
        std::filesystem::create_directories(dir/"Movies");
        const auto path=dir/"movie.mad";
        const auto original=fixture();write(path,original);
        { nfsu::MovieReader m(path.string());nfsu::MovieFrame f;
        require(m.next_video(f)&&f.width==16&&f.height==16&&f.timestamp_ms==0&&f.duration_ms==33,"frame metadata");
        for(auto v:f.yuv)require(v==128,"DC transform");
        require(!m.next_video(f),"video end");
        std::vector<std::int16_t> pcm;require(m.next_audio(pcm)&&pcm.size()==56,"audio decode");
        for(unsigned i=0;i<28;++i)require(pcm[i*2]==int(i)*100&&pcm[i*2+1]==-int(i)*100,"PCM reset");
        require(!m.next_audio(pcm),"audio end");
        std::vector<std::uint8_t> dest(32*32*4+32,23);nfsu::blit_movie(f,dest.data()+16,32,32,128);
        for(unsigned i=0;i<16;++i)require(dest[i]==23&&dest[dest.size()-1-i]==23,"blit bounds");
        require(dest[16+3]==255,"blit alpha"); }
        auto bad=original;bad[4]=7;rejects(path,bad);
        bad=original;bad.resize(25);rejects(path,bad);
        bad=original;bad[16]=255;rejects(path,bad);
        bad=original;bad[0]='X';rejects(path,bad);
        bad=original;bad[24]=bad[25]=0;rejects(path,bad);
        bad=original;bad.resize(bad.size()-9);rejects(path,bad,true);
        for(const char* name:{"na_boot.mad","PSA.mad","e3_title.mad"})write(dir/"Movies"/name,fixture());
        const auto normal=[] {return nfsu::IntroControl::continue_playback;};
        const auto noop=[](const nfsu::MovieFrame&){};
        auto r=nfsu::play_original_intro(dir.string(),[]{return nfsu::IntroControl::skip;},noop);
        require(r.outcome==nfsu::IntroOutcome::skipped&&starts==0,"initial skip");
        r=nfsu::play_original_intro(dir.string(),[]{return nfsu::IntroControl::quit;},noop);
        require(r.outcome==nfsu::IntroOutcome::quit&&starts==0,"initial quit");
        unsigned shown=0;rendered.clear();
        r=nfsu::play_original_intro(dir.string(),normal,[&](const nfsu::MovieFrame&){++shown;});
        require(r.outcome==nfsu::IntroOutcome::completed&&r.movies_completed==3&&shown==3&&starts==3&&stops==3,"sequence completion/output cleanup");
        require(rendered.size()>=62,"rendered audio missing");
        for(unsigned i=0;i<31;++i){const unsigned phase=i*44100;const unsigned sample=std::min(27U,phase/48000);const unsigned next=std::min(27U,sample+1);const int expected=int((sample*100ULL*(48000-phase%48000)+next*100ULL*(phase%48000))/48000);require(rendered[i*2]==expected&&rendered[i*2+1]==-expected,"44.1kHz resampling");}
        shown=0;r=nfsu::play_original_intro(dir.string(),[&]{return shown?nfsu::IntroControl::skip:nfsu::IntroControl::continue_playback;},[&](const nfsu::MovieFrame&){++shown;});
        require(r.outcome==nfsu::IntroOutcome::skipped&&starts==stops,"active skip cleanup");
        r=nfsu::play_original_intro(dir.string(),normal,[](const nfsu::MovieFrame&){throw std::runtime_error("presentation failed");});
        require(r.outcome==nfsu::IntroOutcome::error&&starts==stops,"presentation error cleanup");
        r=nfsu::play_original_intro((dir/"absent").string(),normal,noop);
        require(r.outcome==nfsu::IntroOutcome::error&&starts==stops,"missing media cleanup");
        std::filesystem::remove_all(dir);std::puts("PASS intro decoder/player lifecycle");return 0;
    } catch(const std::exception& e){std::filesystem::remove_all(dir);std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
