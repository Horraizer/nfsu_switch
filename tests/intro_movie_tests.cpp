#include "nfsu/intro_movie.h"
#include "nfsu/sha256.h"
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace {
struct Pin {
    const char* name;std::size_t video_frames;std::uint64_t audio_frames;
    unsigned rate;const char* video;const char* audio;const char* reference_audio;
};
// Full YUV and complete 28-sample PCM blocks independently checked against
// FFmpeg 8.1.1. Native audio pins include the final partial block, which that
// reference decoder discards. Header counts and reset/ADPCM tails are tested.
constexpr Pin pins[]={
 {"na_boot.mad",390,641780,48000,
  "A56D07A46358DD4AA110907A49B2DC621B9127C0CF1B240D45843E7A9ED3FF35",
  "26455FCBD70E9CCB7A2E51B121C355397BFC85B7887361BD4B80E3DBE22E6B6D",
  "3DF867DB5ACA8C99768C0756FF167BFC62DD89143CA9E306753209C9265C9FC3"},
 {"PSA.mad",544,800479,44100,
  "660C28696F1C52794963A3DFABC77FEDB11EA624445192D8281DB35E18B819BF",
  "62F8FFC959CD1EB9BC7386F8910FE9568FA7E1CCA268C1FAB37A133DFB2AC99A",
  "894F99EDCC482F602004ED24B93BFFBEF8DD615DEDFF3DA4C0EFCD1F478AC247"},
 {"e3_title.mad",2426,3569785,44100,
  "61A7CD9AADB2F7ABD0AE05696179674F943A1D3AF1310D6BBBD8166D4295B7BD",
  "A12B73FE73444A3DE6FD6106E0290D8EA4730D91AFCBACDE6C9F447B2626F2AA",
  "26C31B1A89942C6768A55071C32FC3EDE462CCFA2867CC9FDFDCFD0449E9BD58"}
};
void dump_yuv(const std::string& path) {
#ifdef _WIN32
    if(_setmode(_fileno(stdout),_O_BINARY)==-1)throw std::runtime_error("binary stdout unavailable");
#endif
    nfsu::MovieReader reader(path);nfsu::MovieFrame frame;
    unsigned width=0,height=0;
    while(reader.next_video(frame)) {
        if(!width){width=frame.width;height=frame.height;}
        if(frame.width!=width||frame.height!=height)
            throw std::runtime_error("encoded movie changes sample dimensions");
        if(std::fwrite(frame.yuv.data(),1,frame.yuv.size(),stdout)!=frame.yuv.size())
            throw std::runtime_error("movie export write failed");
    }
    if(!width||std::fflush(stdout))throw std::runtime_error("movie export failed");
}
void check_enhanced(const std::string& original,const std::string& cache) {
    nfsu::MovieReader native(original),enhanced(original,cache);
    nfsu::MovieFrame base,high;
    std::size_t frames=0;
    while(native.next_video(base)) {
        if(!enhanced.next_video(high)||high.width!=960||high.height!=720||
           high.timestamp_units!=base.timestamp_units||high.duration_units!=base.duration_units)
            throw std::runtime_error("enhanced movie differs from the native timeline");
        ++frames;
    }
    if(!frames||enhanced.next_video(high))throw std::runtime_error("enhanced movie frame count differs");
    if(native.audio_frames()!=enhanced.audio_frames()||native.audio_rate()!=enhanced.audio_rate())
        throw std::runtime_error("enhanced movie audio header differs");
    std::printf("PASS enhanced movie frames=%zu\n",frames);
}
void check(const std::string& path,const Pin* pin) {
    nfsu::MovieReader reader(path);nfsu::MovieFrame frame;
    nfsu::Sha256 video,audio,reference_audio;
    std::size_t frames=0;std::uint64_t duration=0,pcm_frames=0;
    while(reader.next_video(frame)) {
        video.update(frame.yuv.data(),frame.yuv.size());++frames;
        duration=frame.timestamp_ms+frame.duration_ms;
    }
    std::vector<std::int16_t> pcm;
    while(reader.next_audio(pcm)) {
        audio.update(pcm.data(),pcm.size()*2);
        reference_audio.update(pcm.data(),(pcm.size()/2/28*28)*4);
        pcm_frames+=pcm.size()/2;
    }
    const auto vh=nfsu::sha256_hex(video.finish()),ah=nfsu::sha256_hex(audio.finish()),rh=nfsu::sha256_hex(reference_audio.finish());
    if(!frames||pcm_frames!=reader.audio_frames())throw std::runtime_error("empty/mismatched media");
    std::printf("video_frames=%zu duration_ms=%llu video_sha256=%s\n",frames,
        static_cast<unsigned long long>(duration),vh.c_str());
    std::printf("audio_frames=%llu rate=%u audio_sha256=%s\n",
        static_cast<unsigned long long>(pcm_frames),reader.audio_rate(),ah.c_str());
    std::printf("reference_audio_sha256=%s\n",rh.c_str());
    if(pin && (frames!=pin->video_frames || pcm_frames!=pin->audio_frames || reader.audio_rate()!=pin->rate || vh!=pin->video || ah!=pin->audio || rh!=pin->reference_audio))
        throw std::runtime_error("retail movie pin differs");
}
}
int main(int argc,char** argv) {
 try {
    if(argc==3&&std::string(argv[1])=="--inspect")check(argv[2],nullptr);
    else if(argc==3&&std::string(argv[1])=="--export-yuv"){dump_yuv(argv[2]);return 0;}
    else if(argc==4&&std::string(argv[1])=="--verify-enhanced")check_enhanced(argv[2],argv[3]);
    else if(argc==2)for(const auto& pin:pins){std::puts(pin.name);check(std::string(argv[1])+"/Movies/"+pin.name,&pin);}
    else throw std::runtime_error("usage: nfsu_intro_movie_tests retail-root | --inspect movie.mad | --export-yuv movie.mad | --verify-enhanced movie.mad movie.nfm");
    std::puts("PASS intro movie decode");return 0;
 } catch(const std::exception& e) {std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
