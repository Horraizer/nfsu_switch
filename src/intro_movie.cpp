// Native EA MAD streaming decode. Transform/VLC portions are adapted from
// FFmpeg n7.1 eamad.c, eaidct.c and mpeg12data.c, copyright (c) 2007-2009
// Peter Ross and (c) 2001-2003 The FFmpeg project.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "nfsu/intro_movie.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace nfsu {
namespace {
#include "intro_movie_tables.h"
constexpr unsigned zigzag[64] = {
 0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,
 12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
 35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
 58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
using Bytes = std::vector<std::uint8_t>;
[[noreturn]] void bad(const char* reason) { throw std::runtime_error(std::string("movie: ")+reason); }
void range(const Bytes& b, std::size_t p, std::size_t n) {
    if (p>b.size() || n>b.size()-p) bad("truncated chunk");
}
unsigned le16(const Bytes& b, std::size_t p) { range(b,p,2); return b[p]|(unsigned(b[p+1])<<8); }
std::uint32_t le32(const Bytes& b, std::size_t p) {
    range(b,p,4); return b[p]|(std::uint32_t(b[p+1])<<8)|
        (std::uint32_t(b[p+2])<<16)|(std::uint32_t(b[p+3])<<24);
}
int clip(int x) { return std::clamp(x,0,255); }
// Arithmetic right shift, including negative input, without relying on a
// compiler's signed-shift choice. Wide intermediates avoid corrupt-stream UB.
std::int64_t shr(std::int64_t x,unsigned n) {
    return x>=0 ? x/(std::int64_t(1)<<n) : -((-x+(std::int64_t(1)<<n)-1)/(std::int64_t(1)<<n));
}
class Bits {
    const Bytes& b_; std::size_t p_=0;
public:
    explicit Bits(const Bytes& b):b_(b) {}
    unsigned get(unsigned n) {
        if (n>24 || p_>b_.size()*8 || n>b_.size()*8-p_) bad("truncated bitstream");
        unsigned value=0;
        while(n--) { const auto byte=(p_/8)^1U;
            if(byte>=b_.size()) bad("odd MAD bitstream");
            value=(value<<1)|((b_[byte]>>(7-(p_%8)))&1U); ++p_; }
        return value;
    }
    int signed_get(unsigned n) { const unsigned v=get(n); return (v&(1U<<(n-1))) ? int(v)-int(1U<<n) : int(v); }
    unsigned symbol() {
        struct Node { std::array<unsigned,2> child{}; unsigned symbol=113; };
        static const auto tree=[] {
            std::array<Node,512> nodes{}; unsigned used=1;
            for(unsigned i=0;i<113;++i) {
                unsigned node=0;
                for(unsigned n=vlc[i][1];n;--n) {
                    const unsigned bit=(vlc[i][0]>>(n-1))&1U;
                    if(!nodes[node].child[bit]) nodes[node].child[bit]=used++;
                    node=nodes[node].child[bit];
                }
                nodes[node].symbol=i;
            }
            return nodes;
        }();
        unsigned node=0;
        for(unsigned n=0;n<16;++n) {
            node=tree[node].child[get(1)];
            if(!node) bad("invalid AC VLC");
            if(tree[node].symbol<113) return tree[node].symbol;
        }
        bad("invalid AC VLC");
    }
    int motion() { if(!get(1)) return 0; const bool negative=get(1)!=0; return (negative?-17:0)+int(get(4))+1; }
};
void transform(const std::int16_t* s,int step,std::int64_t* d) {
    const std::int64_t a1=s[step]+s[7*step],a7=s[step]-s[7*step];
    const std::int64_t a5=s[5*step]+s[3*step],a3=s[5*step]-s[3*step];
    const std::int64_t a2=s[2*step]+s[6*step],a6=shr(181LL*(s[2*step]-s[6*step]),8);
    const std::int64_t a0=s[0]+s[4*step],a4=s[0]-s[4*step];
    const auto t0=shr(473*a7-196*a3,9),t1=shr(181*(a1-a5),8),t2=shr(473*a3+196*a7,9);
    const auto b0=t0+a1+a5,b1=t0+t1,b2=t2+t1,b3=t2;
    d[0]=a0+a2+a6+b0; d[1]=a4+a6+b1; d[2]=a4-a6+b2; d[3]=a0-a2-a6+b3;
    d[4]=a0-a2-a6-b3; d[5]=a4-a6-b2; d[6]=a4+a6-b1; d[7]=a0+a2+a6-b0;
}
void idct(std::uint8_t* dest,unsigned stride,std::array<std::int16_t,64>& block) {
    block[0]=static_cast<std::int16_t>(block[0]+4);
    std::array<std::int16_t,64> temp{}; std::int64_t column[8];
    for(unsigned x=0;x<8;++x) {
        transform(block.data()+x,8,column);
        for(unsigned y=0;y<8;++y) temp[y*8+x]=static_cast<std::int16_t>(column[y]);
    }
    for(unsigned y=0;y<8;++y) {
        transform(temp.data()+8*y,1,column);
        for(unsigned x=0;x<8;++x) dest[y*stride+x]=static_cast<std::uint8_t>(clip(int(shr(column[x],4))));
    }
}
Bytes chunk(std::ifstream& f,std::uint64_t size,std::uint64_t& cursor) {
    if(cursor==size) return {};
    if(cursor>size || size-cursor<8) bad("truncated chunk header");
    f.clear(); f.seekg(static_cast<std::streamoff>(cursor));
    Bytes b(8); if(!f.read(reinterpret_cast<char*>(b.data()),8)) bad("read failed");
    const auto n=le32(b,4);
    if(n<8 || n>1024*1024 || n>size-cursor) bad("invalid chunk size");
    b.resize(n); if(!f.read(reinterpret_cast<char*>(b.data()+8),n-8)) bad("read failed");
    cursor+=n; return b;
}
bool tag(const Bytes& b,const char* t) { return b.size()>=4 && std::memcmp(b.data(),t,4)==0; }
std::int16_t be_sample(const Bytes& b,std::size_t p) { range(b,p,2); return static_cast<std::int16_t>((unsigned(b[p])<<8)|b[p+1]); }
}
struct MovieReader::Impl {
    std::ifstream video,audio;
    std::uint64_t size=0,vcursor=0,acursor=0,timestamp=0,total_audio=0,decoded_audio=0;
    unsigned rate=0,channels=0,w=0,h=0;
    Bytes previous;
    std::array<std::int32_t,2> h1{},h2{};
    explicit Impl(const std::string& path):video(path,std::ios::binary),audio(path,std::ios::binary) {
        if(!video || !audio) bad("cannot open retail movie");
        video.seekg(0,std::ios::end); const auto end=video.tellg();
        if(end<8 || end>512LL*1024*1024) bad("invalid movie size");
        size=static_cast<std::uint64_t>(end);
        std::uint64_t p=0;
        // The native audio header occurs beside the first keyframe. Refuse a
        // missing/late header before starting an output device.
        for(unsigned i=0;i<8 && p<size;++i) {
            const auto b=chunk(video,size,p);
            if(i==0 && !tag(b,"MADk")) bad("missing initial MAD keyframe");
            if(!tag(b,"SCHl")) continue;
            range(b,8,4); if(b[8]!='P'||b[9]!='T') bad("unsupported audio header");
            std::array<unsigned,256> values{}; std::size_t q=12;
            bool ended=false;
            while(q<b.size()) {
                unsigned t=b[q++]; if(t==255||t==254) { ended=true; break; }
                if(t==252||t==253) continue;
                range(b,q,1); unsigned n=b[q++];
                if(n>4) bad("unsupported audio patch");
                range(b,q,n);
                for(unsigned j=0;j<n;++j) values[t]=(values[t]<<8)|b[q++];
            }
            if(!ended || values[128]!=2 || (values[160] && values[160]!=10)) bad("unsupported movie audio codec");
            channels=values[130]; rate=values[132]; total_audio=values[133];
            if(channels!=2 || (rate!=44100 && rate!=48000) || !total_audio || total_audio>100000000) bad("invalid movie audio format");
            break;
        }
        if(!rate) bad("missing movie audio header");
    }
    void decode_video(const Bytes& b,MovieFrame& frame) {
        range(b,0,26); const auto nw=le16(b,16),nh=le16(b,18),duration=le32(b,12);
        if(nw<16||nh<16||nw>1024||nh>1024||nw%16||nh%16||duration<65535U||duration>1000U*65535U) bad("invalid video dimensions or duration");
        const bool inter=!tag(b,"MADk");
        if(inter && (nw!=w||nh!=h||previous.empty())) bad("missing video reference");
        w=nw; h=nh; const unsigned luma=w*h;
        frame.width=w;frame.height=h;frame.duration_units=duration;frame.timestamp_units=timestamp;
        frame.duration_ms=duration/65535;frame.timestamp_ms=timestamp/65535;
        frame.yuv.assign(luma*3/2,0);
        Bytes payload(b.begin()+24,b.end()); Bits bits(payload);
        std::array<int,64> quant{};
        quant[0]=(aan[0]*intra[0])>>11;
        for(unsigned i=1;i<64;++i) quant[i]=(aan[i]*intra[i]*int(b[21])+32)>>10;
        for(unsigned my=0;my<h/16;++my) for(unsigned mx=0;mx<w/16;++mx) {
            unsigned map=0; int dx=0,dy=0;
            if(inter) {
                const auto type=bits.get(1)?0U:(bits.get(1)?1U:2U);
                if(type<2) { map=type?bits.get(6):63; dx=bits.motion();dy=bits.motion(); }
            }
            for(unsigned j=0;j<6;++j) {
                const unsigned stride=j<4?w:w/2;
                const unsigned x=j<4?mx*16+(j%2)*8:mx*8;
                const unsigned y=j<4?my*16+(j/2)*8:my*8;
                const unsigned base=j<4?0:luma+(j-4)*luma/4;
                auto* dest=frame.yuv.data()+base+y*stride+x;
                if(map&(1U<<j)) {
                    const int add=2*bits.motion();
                    const int sx=int(x)+(j<4?dx:dx/2),sy=int(y)+(j<4?dy:dy/2);
                    const int plane_height=j<4?int(h):int(h/2);
                    if(sx<0||sy<0||sx+8>int(stride)||sy+8>plane_height) bad("motion outside reference");
                    const auto* src=previous.data()+base+unsigned(sy)*stride+unsigned(sx);
                    for(unsigned yy=0;yy<8;++yy) for(unsigned xx=0;xx<8;++xx) dest[yy*stride+xx]=static_cast<std::uint8_t>(clip(src[yy*stride+xx]+add));
                } else {
                    std::array<std::int16_t,64> block{};
                    block[0]=static_cast<std::int16_t>((128+bits.signed_get(8))*quant[0]);
                    unsigned coefficient=0;
                    for(;;) {
                        const auto symbol=bits.symbol(); if(symbol==112) break;
                        int level; unsigned run;
                        if(symbol==111) { level=bits.signed_get(10);run=bits.get(6)+1; }
                        else {level=levels[symbol];run=unsigned(runs[symbol])+1;}
                        coefficient+=run; if(coefficient>63) bad("coefficient run overflow");
                        const unsigned z=zigzag[coefficient];
                        const bool negative=level<0; const int magnitude=negative?-level:level;
                        int value=((magnitude*quant[z])>>4); value=(value-1)|1;
                        if(symbol==111) { if(negative) value=-value; }
                        else if(bits.get(1)) value=-value;
                        block[z]=static_cast<std::int16_t>(value);
                    }
                    idct(dest,stride,block);
                }
            }
        }
        if(!tag(b,"MADe")) previous=frame.yuv;
        timestamp+=duration;
        frame.rgba.resize(luma*4);
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
            const int yy=int(frame.yuv[y*w+x])-16;
            const int u=int(frame.yuv[luma+(y/2)*(w/2)+x/2])-128;
            const int v=int(frame.yuv[luma+luma/4+(y/2)*(w/2)+x/2])-128;
            const auto p=(y*w+x)*4;
            frame.rgba[p]=static_cast<std::uint8_t>(clip((298*yy+409*v+128)>>8));
            frame.rgba[p+1]=static_cast<std::uint8_t>(clip((298*yy-100*u-208*v+128)>>8));
            frame.rgba[p+2]=static_cast<std::uint8_t>(clip((298*yy+516*u+128)>>8));
            frame.rgba[p+3]=255;
        }
    }
    void decode_audio(const Bytes& b,std::vector<std::int16_t>& out) {
        const auto frames=le32(b,8); const std::size_t base=20;
        if(!frames||frames>65536||decoded_audio+frames>total_audio) bad("invalid audio frame count");
        std::array<std::size_t,3> start{{base+le32(b,12),base+le32(b,16),b.size()}};
        if(start[0]>start[1]||start[1]>start[2]) bad("invalid channel offsets");
        out.resize(std::size_t(frames)*2);
        constexpr int c1[4]={0,240,460,392},c2[4]={0,0,-208,-220};
        for(unsigned ch=0;ch<2;++ch) {
            std::size_t p=start[ch];unsigned done=0;
            while(done<frames) {
                if(p>=start[ch+1]) bad("truncated audio frame");
                const unsigned control=b[p],take=std::min(28U,frames-done);
                if(control==238) {
                    if(start[ch+1]-p<61) bad("truncated audio PCM reset");
                    h1[ch]=be_sample(b,p+1);h2[ch]=be_sample(b,p+3);
                    for(unsigned i=0;i<take;++i) out[(done+i)*2+ch]=be_sample(b,p+5+i*2);
                    p+=61;
                } else {
                    if(start[ch+1]-p<15||control/16>3) bad("invalid audio ADPCM frame");
                    for(unsigned i=0;i<take;++i) {
                        const unsigned packed=b[p+1+i/2],raw=i%2?packed&15:packed>>4;
                        const int nibble=raw<8?int(raw):int(raw)-16;
                        const auto value=shr(std::int64_t(nibble)*(1<<(20-(control&15)))+c1[control/16]*h1[ch]+c2[control/16]*h2[ch],8);
                        h2[ch]=h1[ch];h1[ch]=int(std::clamp<std::int64_t>(value,-32768,32767));
                        out[(done+i)*2+ch]=static_cast<std::int16_t>(h1[ch]);
                    }
                    p+=15;
                }
                done+=take;
            }
        }
        decoded_audio+=frames;
    }
};
MovieReader::MovieReader(const std::string& path):impl_(std::make_unique<Impl>(path)) {}
MovieReader::~MovieReader()=default;
bool MovieReader::next_video(MovieFrame& frame) {
    auto& p=*impl_;
    while(p.vcursor<p.size) {const auto b=chunk(p.video,p.size,p.vcursor);
        if(tag(b,"MADk")||tag(b,"MADm")||tag(b,"MADe")) {p.decode_video(b,frame);return true;}}
    return false;
}
bool MovieReader::next_audio(std::vector<std::int16_t>& pcm) {
    auto& p=*impl_;
    while(p.acursor<p.size) {const auto b=chunk(p.audio,p.size,p.acursor);
        if(tag(b,"SCDl")) {p.decode_audio(b,pcm);return true;}}
    if(p.decoded_audio!=p.total_audio) bad("audio duration differs from header");
    pcm.clear();return false;
}
unsigned MovieReader::audio_rate() const noexcept{return impl_->rate;}
std::uint64_t MovieReader::audio_frames() const noexcept{return impl_->total_audio;}
void blit_movie(const MovieFrame& f,std::uint8_t* dest,unsigned w,unsigned h,std::size_t stride) {
    if(!dest||!w||!h||stride<std::size_t(w)*4||!f.width||!f.height||f.rgba.size()!=std::size_t(f.width)*f.height*4) bad("invalid movie destination");
    for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {auto* p=dest+y*stride+x*4;p[0]=p[1]=p[2]=0;p[3]=255;}
    if(bool(f.display_width)!=bool(f.display_height)) bad("incomplete movie display aspect");
    const unsigned aspect_w=f.display_width?f.display_width:f.width;
    const unsigned aspect_h=f.display_height?f.display_height:f.height;
    unsigned dw=w,dh=unsigned(std::uint64_t(w)*aspect_h/aspect_w);
    if(std::uint64_t(w)*aspect_h>std::uint64_t(h)*aspect_w){dh=h;dw=unsigned(std::uint64_t(h)*aspect_w/aspect_h);}
    if(!dw||!dh)return;
    const unsigned left=(w-dw)/2,top=(h-dh)/2;
    for(unsigned y=0;y<dh;++y) for(unsigned x=0;x<dw;++x) {
        const auto src=(std::size_t(y)*f.height/dh*f.width+std::size_t(x)*f.width/dw)*4;
        std::memcpy(dest+(top+y)*stride+(left+x)*4,f.rgba.data()+src,4);
    }
}
} // namespace nfsu
