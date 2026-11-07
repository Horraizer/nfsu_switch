#include "nfsu/race_hud.h"
#include "nfsu/texture.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace nfsu {
void DriftScoreDisplay::sample(const DriftScorePublication& publication,std::int32_t now,unsigned zone) noexcept {
    if(publication.sequence!=sequence) {
        sequence=publication.sequence;points=publication.points;multiplier=publication.multiplier;
        failed=publication.failed;shown=now;visible=sequence!=0;
    }
    if(visible) {
        if(failed) {
            if(float(std::int64_t(now)-shown)*.00025F>1.F)visible=false;
        }else if(now!=shown)visible=false;
    }
    color=(failed||points<0)?std::array<unsigned char,4>{{128,0,0,255}}:
        zone>1?std::array<unsigned char,4>{{255,128,32,255}}:std::array<unsigned char,4>{{255,255,0,255}};
}
namespace {
unsigned word(const unsigned char* p) {return p[0]|unsigned(p[1])<<8;}
std::uint32_t dword(const unsigned char* p) {return word(p)|std::uint32_t(word(p+2))<<16;}
float real(const unsigned char* p) {float f;std::memcpy(&f,p,4);return f;}
std::string text(const unsigned char* p,std::size_t n) {
    const auto* end=static_cast<const unsigned char*>(std::memchr(p,0,n));
    if(!end)throw std::runtime_error("drift animation string unterminated");
    return {reinterpret_cast<const char*>(p),std::size_t(end-p)};
}
bool retained(const std::string& name) {
    for(const char* n:{"drift_multiplier_text","SP_DRIFT_TOTAL","Style_TotalDrift_Title",
                      "SlowMessage","WrongWay_CountDown","WrongWayText"})if(name==n)return true;
    return false;
}
void scripts(const unsigned char* p,std::size_t size,const std::string& object,
             std::vector<DriftHudAnimation>& out) {
    for(std::size_t at=0;at<size;) {
        if(size-at<8)throw std::runtime_error("drift script header truncated");
        const auto length=dword(p+at+4);
        if(length>size-at-8)throw std::runtime_error("drift script outside object");
        if(!std::memcmp(p+at,"Scrp",4)) {
            DriftHudAnimation animation;animation.object=object;DriftHudChannel channel;
            for(std::size_t f=at+8;f<at+8+length;) {
                if(at+8+length-f<4)throw std::runtime_error("drift script field truncated");
                const auto n=word(p+f+2);const auto* v=p+f+4;
                if(n>at+8+length-f-4)throw std::runtime_error("drift script field outside object");
                if(!std::memcmp(p+f,"Sn",2))animation.state=text(v,n);
                if(!std::memcmp(p+f,"Sh",2) && n==16)animation.duration=dword(v+4);
                if(!std::memcmp(p+f,"Sc",2) && n==4) {
                    // FEng chains Highlight to Init (hash 0x1744B3).
                    if(dword(v)==0x1744b3)animation.return_to_init=true;
                    else if(!(object=="Shift_light"&&dword(v)==0x2a15f4b9))
                        throw std::runtime_error("drift state chain unsupported");
                }
                if(!std::memcmp(p+f,"FI",2) && n==8) {
                    channel={};channel.kind=v[0];channel.duration=dword(v+4)&0xffffff;
                    channel.loop=v[3]!=0;
                    if(v[2]!=1 || (channel.kind!=4 && channel.kind!=6) ||
                       v[1]!=(channel.kind==4?12:16))throw std::runtime_error("drift key format unsupported");
                }
                if(!std::memcmp(p+f,"To",2) && n==4)channel.target=dword(v);
                if(!std::memcmp(p+f,"Kd",2)) {
                    const unsigned dims=channel.kind==4?3:4,stride=4+4*dims;
                    if(n<stride || n%stride || dword(v)!=0xffffffffU ||
                       !((channel.kind==6 && channel.target==0)||(channel.kind==4 && (channel.target==14||channel.target==7))))
                        throw std::runtime_error("drift key target or base unsupported");
                    for(unsigned k=0;k<n/stride;++k) {
                        DriftHudKey key;key.tick=dword(v+k*stride);
                        for(unsigned c=0;c<dims;++c) {
                            key.value[c]=channel.kind==6?float(std::int32_t(dword(v+k*stride+4+c*4))):real(v+k*stride+4+c*4);
                            if(!std::isfinite(key.value[c]))throw std::runtime_error("drift key nonfinite");
                        }
                        if(!k)channel.base=key.value;
                        else {
                            if(key.tick>100000 || (!channel.keys.empty() && key.tick<=channel.keys.back().tick))
                                throw std::runtime_error("drift key order invalid");
                            channel.keys.push_back(key);
                        }
                    }
                    animation.channels.push_back(std::move(channel));
                }
                f+=4+n;
            }
            if(animation.state.empty() || animation.channels.empty())throw std::runtime_error("drift state empty");
            out.push_back(std::move(animation));
        }
        at+=8+length;
    }
}
}
static bool load_hud_animations(const std::vector<unsigned char>& ingame,
                               std::vector<DriftHudAnimation>& result,std::string& error,bool drag) {
    try {
        // USA1.4 INGAMEB package offset, authenticated by the FNG object set.
        const std::size_t at=drag?0x16d50c:0x17140c;
        if(ingame.size()<at+16 || std::memcmp(ingame.data()+at,"JDLZ",4))throw std::runtime_error("drift FNG absent");
        const auto length=dword(ingame.data()+at+12);
        if(length>ingame.size()-at)throw std::runtime_error("drift FNG outside input");
        std::vector<unsigned char> fng;std::size_t used=0;
        if(!jdlz_decompress(ingame.data()+at,length,1024*1024,fng,used,error))return false;
        std::vector<DriftHudAnimation> out;unsigned objects=0;
        for(std::size_t pos=0;pos+16<=fng.size();++pos) {
            if(std::memcmp(fng.data()+pos,"FOb\xEA",4))continue;
            const auto outer=dword(fng.data()+pos+4),n=dword(fng.data()+pos+12);
            if(std::memcmp(fng.data()+pos+8,"ObjD",4) || outer<n+8 || outer>fng.size()-pos-8 || n>fng.size()-pos-16)
                throw std::runtime_error("drift FNG object bounds");
            std::string name;
            for(std::size_t p=pos+16;p<pos+16+n;) {
                if(pos+16+n-p<4)throw std::runtime_error("drift object field header");
                const auto size=word(fng.data()+p+2);
                if(size>pos+16+n-p-4)throw std::runtime_error("drift object field bounds");
                if(!std::memcmp(fng.data()+p,"On",2))name=text(fng.data()+p+4,size);
                p+=4+size;
            }
            if(drag?(name=="ShiftMessage"||name=="konopaoverheat(119)"||name=="Shift_light"):retained(name)) {
                scripts(fng.data()+pos+16+n,outer-n-8,name,out);++objects;
            }
            pos+=7+outer;
        }
        if(objects!=(drag?3U:6U))throw std::runtime_error("HUD animation object set incomplete");
        result=std::move(out);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool load_drift_hud_animations(const std::vector<unsigned char>& ingame,
    std::vector<DriftHudAnimation>& result,std::string& error) {
    return load_hud_animations(ingame,result,error,false);
}
bool load_drag_hud_animations(const std::vector<unsigned char>& ingame,
    std::vector<DriftHudAnimation>& result,std::string& error) {
    return load_hud_animations(ingame,result,error,true);
}
DriftHudAppearance sample_hud_animation(const std::vector<DriftHudAnimation>& animations,const char* object,
                                              const char* state,unsigned ticks) noexcept {
    DriftHudAppearance out;
    const auto apply=[&](const DriftHudAnimation& a,unsigned time) {
        for(const auto& c:a.channels) {
            const unsigned tick=c.loop && c.duration?time%c.duration:std::min(time,c.duration);
            auto value=c.base;
            if(!c.keys.empty()) {
                const auto* before=&c.keys.front();const auto* after=before;
                for(const auto& key:c.keys){after=&key;if(key.tick>=tick)break;before=&key;}
                const double f=after->tick>before->tick?double(tick-before->tick)/(after->tick-before->tick):0;
                for(unsigned k=0;k<(c.kind==4?3U:4U);++k) {
                    const double delta=(after->value[k]-before->value[k])*f;
                    value[k]+=before->value[k]+float(c.kind==6?int(delta+.5):delta);
                }
            }
            if(c.target==0) {
                for(unsigned k=0;k<3;++k)out.color[k]=static_cast<unsigned char>(std::clamp(value[2-k],0.F,255.F));
                out.alpha=static_cast<unsigned char>(std::clamp(value[3],0.F,255.F));
            }else if(c.target==14)out.scale=value[0];
            else if(c.target==7){out.positioned=true;std::copy_n(value.begin(),3,out.position.begin());}
        }
    };
    for(const auto& a:animations)if(a.object==object && a.state=="Init")apply(a,0);
    for(const auto& a:animations)if(a.object==object && a.state==state) {
        if(!a.return_to_init || ticks<a.duration)apply(a,ticks);
        break;
    }
    return out;
}
DriftHudAppearance sample_drift_hud_animation(const RaceHudAtlas& atlas,const char* object,
                                              const char* state,unsigned ticks) noexcept {
    return sample_hud_animation(atlas.drift_animations,object,state,ticks);
}
} // namespace nfsu
