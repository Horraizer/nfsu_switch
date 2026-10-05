#include "nfsu/frontend.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/frontend_motion.h"
#include "nfsu/race_events.h"
#include "nfsu/race_lap_hud_presentation.h"
#include "nfsu/sha256.h"
#include "nfsu/texture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
namespace nfsu {
FrontendAction FrontendMenu::update(FrontendInput input) {
    if(input.back) return FrontendAction::exit;
    if(input.up!=input.down) { selected_=1-selected_;notice_.clear(); }
    if(!input.accept) return FrontendAction::none;
    return selected_==0?FrontendAction::underground:FrontendAction::free_drive;
}
namespace {
unsigned word(const unsigned char* p) {return unsigned(p[0])|unsigned(p[1])<<8;}
unsigned dword(const unsigned char* p) {return word(p)|word(p+2)<<16;}
float real(const unsigned char* p) {float v;std::memcpy(&v,p,4);return v;}
std::string ascii(const unsigned char* p,std::size_t n) {
    std::string s;for(std::size_t i=0;i<n && p[i];++i) {
        if(p[i]<32 || p[i]>126) throw std::runtime_error("invalid original frontend ASCII string");
        s+=char(p[i]);
    }return s;
}
struct Image {unsigned width=0,height=0,atlas_x=0,atlas_y=0;std::vector<unsigned char> rgba;TextureAlphaBlend blend=TextureAlphaBlend::source_over;};
struct UiKey {int tick=0;std::array<double,4> value{};};
struct UiLoop {unsigned kind=0,target=0,duration=0;std::array<double,4> base{};std::vector<UiKey> keys;};
struct UiState {std::string name;std::vector<UiLoop> tracks;std::vector<std::array<unsigned,3>> events;};
struct Object {
    unsigned id=0,parent=0,type=0,resource=0,justification=0;
    std::string name,text;
    std::vector<UiLoop> loops;
    std::vector<UiState> states;
    float x=0,y=0,z=0,w=1,h=1,qz=0,qw=1;
    std::array<unsigned,4> color{{255,255,255,255}};
    std::array<float,4> uv{{0,0,1,1}};
    std::array<std::array<unsigned,4>,4> corner_colors{};
};
// Original FI/Kd loop records, limited to the authored main-menu consumers.
void load_ui_loops(const unsigned char* data,std::size_t length,Object& object) {
    for(std::size_t at=0;at+8<=length;) {
        const unsigned n=dword(data+at+4);
        if(n>length-at-8)throw std::runtime_error("frontend script chunk truncated");
        if(!std::memcmp(data+at,"Scrp",4)) {
            bool init=false,loop=false;UiLoop track;
            for(std::size_t field=at+8;field<at+8+n;) {
                if(at+8+n-field<4)throw std::runtime_error("frontend script field truncated");
                const auto* tag=data+field;const unsigned size=word(tag+2);const auto* v=tag+4;
                if(size>at+8+n-field-4)throw std::runtime_error("frontend script field outside chunk");
                if(!std::memcmp(tag,"Sn",2))init=ascii(v,size)=="Init";
                if(!std::memcmp(tag,"Sh",2) && size==16)loop=dword(v+8)==1;
                if(!std::memcmp(tag,"FI",2) && size==8) {
                    track=UiLoop{};track.kind=v[0];track.duration=dword(v+4)&0xffffff;
                    if(init && loop && (v[1]!=(track.kind==4?12:16) || v[2]!=1 || v[3]!=1))
                        throw std::runtime_error("unsupported native frontend loop frame");
                }
                if(!std::memcmp(tag,"To",2) && size==4)track.target=dword(v);
                if(!std::memcmp(tag,"Kd",2) && init && loop) {
                    const unsigned stride=track.kind==4?16:20;
                    if(!track.duration || size<stride*2 || size%stride ||
                       !((track.kind==6 && track.target==0) || (track.kind==5 && track.target==10) ||
                         (track.kind==4 && (track.target==7 || track.target==14))))
                        throw std::runtime_error("unsupported authored frontend loop keys");
                    for(unsigned key=0;key<size/stride;++key) {
                        UiKey value;value.tick=static_cast<int>(dword(v+key*stride));
                        for(unsigned c=0;c<(track.kind==4?3U:4U);++c)value.value[c]=track.kind==6?
                            double(static_cast<int>(dword(v+key*stride+4+c*4))):double(real(v+key*stride+4+c*4));
                        if(!key) {if(value.tick!=-1)throw std::runtime_error("frontend loop base missing");track.base=value.value;}
                        else {
                            if(value.tick<0 || unsigned(value.tick)>track.duration ||
                            (!track.keys.empty() && value.tick<=track.keys.back().tick))
                                throw std::runtime_error("invalid frontend loop key times");
                            track.keys.push_back(value);}
                    }
                    if(track.keys.empty())
                        throw std::runtime_error("frontend loop endpoints absent");
                    if(track.kind==5) {
                        if(track.base[0]!=0 || track.base[1]!=0)throw std::runtime_error("frontend loop quaternion plane");
                        for(unsigned k=0;k<track.keys.size();++k) {
                            const auto& q=track.keys[k].value;
                            if(q[0]!=0 || q[1]!=0)throw std::runtime_error("frontend quaternion key plane");
                            if(k && std::abs(q[2]*track.keys[k-1].value[2]+q[3]*track.keys[k-1].value[3])>1e-5 &&
                               std::abs(std::abs(q[2]*track.keys[k-1].value[2]+q[3]*track.keys[k-1].value[3])-.70710678)>1e-5)
                                throw std::runtime_error("frontend quaternion native angle requires recovery");
                        }
                    }
                    object.loops.push_back(track);
                }
                field+=size+4;
            }
        }
        at+=n+8;
    }
}
// Authenticated nonloop text entry/fade and selection scripts.
void load_ui_states(const unsigned char* data,std::size_t length,Object& object) {
    for(std::size_t at=0;at+8<=length;) {
        const unsigned n=dword(data+at+4);if(n>length-at-8)throw std::runtime_error("frontend state chunk truncated");
        if(!std::memcmp(data+at,"Scrp",4)) {
            UiState state;UiLoop track;bool nonloop=false;
            for(std::size_t field=at+8;field<at+8+n;) {
                if(at+8+n-field<4)throw std::runtime_error("frontend state field truncated");
                const auto* tag=data+field;const unsigned size=word(tag+2);const auto* v=tag+4;
                if(size>at+8+n-field-4)throw std::runtime_error("frontend state field outside chunk");
                if(!std::memcmp(tag,"Sn",2))state.name=ascii(v,size);
                const bool selection=state.name=="HighLight" || state.name=="UnHighLight";
                const bool text_entry=object.type==2 && (state.name=="Init" || state.name=="FadeIn");
                const bool retained=selection || text_entry;
                if(!std::memcmp(tag,"Sh",2) && size==16)nonloop=dword(v+8)==0;
                if(!std::memcmp(tag,"FI",2) && size==8) {
                    track=UiLoop{};track.kind=v[0];track.duration=dword(v+4)&0xffffff;
                    if(retained && (!nonloop || v[1]!=(track.kind==4?12:16) || v[2]!=1 || v[3]!=0))
                        throw std::runtime_error("unsupported native selection frame");
                }
                if(!std::memcmp(tag,"To",2) && size==4)track.target=dword(v);
                if(!std::memcmp(tag,"EV",2) && selection) {
                    if(size%12)throw std::runtime_error("frontend selection event truncated");
                    for(unsigned e=0;e<size;e+=12)state.events.push_back({{dword(v+e),dword(v+e+4),dword(v+e+8)}});
                }
                if(!std::memcmp(tag,"Kd",2) && retained) {
                    const unsigned stride=track.kind==4?16:20;
                    if(!size || size%stride || !((track.kind==6 && track.target==0) ||
                       (track.kind==4 && (track.target==7 || track.target==14))))
                        throw std::runtime_error("unsupported authored selection keys");
                    for(unsigned k=0;k<size/stride;++k) {
                        UiKey key;key.tick=static_cast<int>(dword(v+k*stride));
                        for(unsigned c=0;c<(track.kind==4?3U:4U);++c)key.value[c]=track.kind==6?
                            double(static_cast<int>(dword(v+k*stride+4+c*4))):double(real(v+k*stride+4+c*4));
                        if(!k) {if(key.tick!=-1)throw std::runtime_error("selection base absent");track.base=key.value;}
                        else {
                            if(key.tick<0 || unsigned(key.tick)>track.duration || (!track.keys.empty() && key.tick<=track.keys.back().tick))
                                throw std::runtime_error("invalid selection key time");
                            track.keys.push_back(key);
                        }
                    }
                    state.tracks.push_back(track);
                }
                field+=size+4;
            }
            if(state.name=="HighLight" || state.name=="UnHighLight" ||
               (object.type==2 && (state.name=="Init" || state.name=="FadeIn")))object.states.push_back(std::move(state));
        }
        at+=n+8;
    }
}
struct Transform {float a=1,b=0,c=0,d=1,x=0,y=0,z=0;};
Transform combine(const Transform& p,const Object& o,bool scale) {
    const float ca=1-2*o.qz*o.qz,sa=2*o.qz*o.qw;
    Transform t;
    const float sx=scale?o.w:1,sy=scale?o.h:1;
    t.a=(p.a*ca+p.c*sa)*sx;t.b=(p.b*ca+p.d*sa)*sx;
    t.c=(p.c*ca-p.a*sa)*sy;t.d=(p.d*ca-p.b*sa)*sy;
    t.x=p.x+p.a*o.x+p.c*o.y;t.y=p.y+p.b*o.x+p.d*o.y;t.z=p.z+o.z;return t;
}
struct Canvas : ScreenImage {
    FrontendUiFrame* gpu=nullptr;
    unsigned atlas_width=0,atlas_height=0;
    Canvas(ScreenImage image):ScreenImage(image){}
    Canvas(unsigned w,unsigned h,const FrontendUiAtlas& atlas,FrontendUiFrame& frame):
        ScreenImage{nullptr,w,h,w*4},gpu(&frame),atlas_width(atlas.width),atlas_height(atlas.height){}
};
// Pixel rasterization of a recovered FEng affine quad; letterforms are FNTF
// glyphs, never synthesized glyphs. Display preserves the 640x480 canvas.
void quad(Canvas target,const Image& tex,const Transform& m,float x,float y,float w,float h,
          std::array<float,4> uv,std::array<unsigned,4> color,
          const std::array<std::array<unsigned,4>,4>* corners=nullptr) {
    if((!target.pixels && !target.gpu) || !w || !h || tex.rgba.empty()) return;
    // Native FEng submit maps its 640x480 canvas independently to NDC.
    // Letterboxing exposes authored control markers outside that canvas.
    const float sx=float(target.width)/640,sy=float(target.height)/480;
    const float det=m.a*m.d-m.b*m.c;
    if(std::abs(det)<0.00001F) return;
    const float xs[4]={x,x+w,x+w,x},ys[4]={y,y,y+h,y+h};
    if(target.gpu) {
        auto& frame=*target.gpu;
        if(frame.additive.size()>=kFrontendUiMaxQuads)throw std::runtime_error("frontend GPU quad capacity exceeded");
        const float us[4]={uv[0],uv[2],uv[2],uv[0]},vs[4]={uv[1],uv[1],uv[3],uv[3]};
        for(unsigned i=0;i<4;++i) {
            TrackVertex vertex;
            vertex.px=(320+m.x+m.a*xs[i]+m.c*ys[i])*sx;
            vertex.py=(240+m.y+m.b*xs[i]+m.d*ys[i])*sy;vertex.pz=-1;
            const auto& modulation=corners?(*corners)[i]:color;
            for(unsigned c=0;c<4;++c)vertex.color[c]=static_cast<unsigned char>(std::min(255U,modulation[c==3?3:2-c])/2*2);
            vertex.u=(tex.atlas_x+std::max(0.F,std::min(1.F,us[i]))*tex.width)/target.atlas_width;
            vertex.v=(tex.atlas_y+std::max(0.F,std::min(1.F,vs[i]))*tex.height)/target.atlas_height;
            frame.vertices.push_back(vertex);
        }
        frame.additive.push_back(tex.blend==TextureAlphaBlend::additive);return;
    }
    float l=1e6F,r=-1e6F,t=1e6F,b=-1e6F;
    for(unsigned i=0;i<4;++i) {
        const float px=(320+m.x+m.a*xs[i]+m.c*ys[i])*sx;
        const float py=(240+m.y+m.b*xs[i]+m.d*ys[i])*sy;
        l=std::min(l,px);r=std::max(r,px);t=std::min(t,py);b=std::max(b,py);
    }
    const int left=std::max(0,int(std::floor(l))),right=std::min(int(target.width),int(std::ceil(r)));
    const int top=std::max(0,int(std::floor(t))),bottom=std::min(int(target.height),int(std::ceil(b)));
    for(int py=top;py<bottom;++py) for(int px=left;px<right;++px) {
        const float rx=(float(px)+.5F)/sx-320-m.x,ry=(float(py)+.5F)/sy-240-m.y;
        const float lx=(m.d*rx-m.c*ry)/det,ly=(m.a*ry-m.b*rx)/det;
        const float u=(lx-x)/w,v=(ly-y)/h;
        if(u<0 || u>=1 || v<0 || v>=1) continue;
        const float tu=uv[0]+u*(uv[2]-uv[0]),tv=uv[1]+v*(uv[3]-uv[1]);
        const auto tx=std::min(tex.width-1,unsigned(std::max(0.F,tu)*float(tex.width)));
        const auto ty=std::min(tex.height-1,unsigned(std::max(0.F,tv)*float(tex.height)));
        const auto* src=tex.rgba.data()+(std::size_t(ty)*tex.width+tx)*4;
        auto* dst=target.pixels+std::size_t(py)*target.stride+std::size_t(px)*4;
        std::array<float,4> modulation{};
        for(unsigned channel=0;channel<4;++channel) {
            if(corners) {
                // Original D3D triangle fan: TL/TR/BR, then TL/BR/BL.
                const float weights[4]={1-std::max(u,v),std::max(0.F,u-v),std::min(u,v),std::max(0.F,v-u)};
                for(unsigned corner=0;corner<4;++corner)
                    modulation[channel]+=weights[corner]*float(std::min(255U,(*corners)[corner][channel])/2)*2;
            } else modulation[channel]=float(std::min(255U,color[channel])/2)*2;
        }
        const unsigned alpha=static_cast<unsigned>(float(src[3])*modulation[3]/255);
        for(unsigned c=0;c<3;++c) {
            // FEng image 0x4F2000 and font 0x4F48B0 pack channels /2;
            // the port's decoded world material applies Modulate2x. Preserve that
            // order rather than doubling the original unhalved color.
            const unsigned value=static_cast<unsigned>(float(src[c])*modulation[2-c]/255);
            const unsigned background=unsigned(dst[c])*(tex.blend==TextureAlphaBlend::additive?255:255-alpha);
            dst[c]=static_cast<unsigned char>(std::min(255U,(value*alpha+background+127)/255));
        }dst[3]=255;
    }
}
bool texture(const std::vector<unsigned char>& bytes,unsigned hash,Image& image,std::string& error) {
    const auto packs=parse_texture_pack_set(bytes.data(),bytes.size());
    if(!packs.ok()) {error="original frontend texture pack invalid";return false;}
    for(const auto& p:packs.packs) for(const auto& rec:p.textures) if(rec.hash==hash) {
        if(!rec.valid || rec.width>2048 || rec.height>2048) {error="invalid original frontend texture record";return false;}
        std::vector<unsigned char> payload;std::size_t decoded=0;std::uint64_t size=0;P8Palette palette;
        if(!texture_payload(bytes.data(),bytes.size(),rec,payload,decoded,error,&palette) ||
           !level_size(rec.format,rec.width,rec.height,size) || size>payload.size() ||
           !decode_level(rec.format,rec.width,rec.height,payload.data(),size,image.rgba,error,&palette)) return false;
        image.width=rec.width;image.height=rec.height;image.blend=texture_alpha_blend(rec);return true;
    }return false;
}
}
struct FrontendArt::Impl {
    Image background;
    FrontendUiAtlas atlas;
    void draw(Canvas image,const FrontendMenu& menu) const;
    std::map<unsigned,Image> textures;
    std::vector<Object> objects;
    RaceLapHudFont large,small;
    unsigned large_hash=0,small_hash=0;
    std::uint64_t ui_ticks=0;bool temporal=false;
    struct ActiveState {unsigned state=0;std::uint64_t start=0;bool settled=false;};
    mutable std::map<unsigned,ActiveState> active_states;
    mutable unsigned last_selection=2;
    void start_named_state(const Object& object,const std::string& name,std::uint64_t start) const {
        for(unsigned i=0;i<object.states.size();++i)if(object.states[i].name==name) {
            active_states[object.id]={i,start,false};return;
        }
    }
    void start_state(const Object& object,bool high,std::uint64_t start,bool settled=false) const {
        const std::string name=high?"HighLight":"UnHighLight";
        for(unsigned i=0;i<object.states.size();++i)if(object.states[i].name==name) {
            active_states[object.id]={i,start,settled};
            for(const auto& event:object.states[i].events) {
                if(event[2]!=0 || event[0]!=(high?0xabc08912U:0x55d1e635U))
                    throw std::runtime_error("unsupported native selection event");
                const auto target=std::find_if(objects.begin(),objects.end(),[&](const Object& o){return o.id==event[1];});
                if(target==objects.end())throw std::runtime_error("selection event target absent");
                start_state(*target,high,start,settled);
            }
            return;
        }
        throw std::runtime_error("authored selection state absent");
    }
    void select(unsigned selected) const {
        if(!temporal || selected==last_selection)return;
        const bool first=last_selection==2;
        const auto start=first?0:ui_ticks;
        for(const auto& object:objects)if(object.name=="UndergroundMode" || object.name=="QuickRace") {
            const bool high=selected==(object.name=="QuickRace"?1U:0U);
            start_state(object,high,start,first && !high);
        }
        // Original main handler4B371D..4B3766 starts Description FadeIn
        // on the selected item HighLight event; switching restarts its keys.
        for(const auto& object:objects)if(object.name=="Description")
            start_named_state(object,"FadeIn",start);
        last_selection=selected;
    }

    Object sample(const Object& original) const {
        Object o=original;
        if(!temporal)return o;
        for(const auto& loop:o.loops) {
            const unsigned tick=unsigned(ui_ticks%loop.duration);
            auto next=std::lower_bound(loop.keys.begin(),loop.keys.end(),int(tick),
                [](const UiKey& key,int t){return key.tick<t;});
            const UiKey* before=nullptr;const UiKey* after=nullptr;double elapsed=0,span=0;
            if(next==loop.keys.end()) {
                before=&loop.keys.back();after=&loop.keys.front();
                elapsed=double(tick-before->tick);span=double(loop.duration)+after->tick-before->tick;
            } else if(next==loop.keys.begin() && tick!=unsigned(next->tick)) {
                before=&loop.keys.back();after=&*next;
                elapsed=double(loop.duration)+tick-before->tick;span=double(loop.duration)+after->tick-before->tick;
            } else {
                after=&*next;before=next==loop.keys.begin()?after:&*(next-1);
                elapsed=double(tick-before->tick);span=double(after->tick-before->tick);
            }
            const auto& a=*before;const auto& b=*after;
            const float fraction=span>0?float(elapsed/span):1.F;
            std::array<double,4> value{};
            if(loop.kind==5) {
                if(tick==unsigned(b.tick))value=b.value;
                else {
                    // Authenticated circle key pairs use native 4FC640 sine phases:
                    // orthogonal16383 and the sole 45-degree pair8191.
                    const double dot=a.value[2]*b.value[2]+a.value[3]*b.value[3];
                    const unsigned angle=std::abs(dot)<1e-5?16383:8191;
                    const float denominator=frontend_fixed_sine(static_cast<std::uint16_t>(angle));
                    const float wb=frontend_fixed_sine(static_cast<std::uint16_t>(double(fraction)*angle))/denominator;
                    const float wa=frontend_fixed_sine(static_cast<std::uint16_t>((1-double(fraction))*angle))/denominator;
                    for(unsigned c=0;c<4;++c)value[c]=double(wa)*a.value[c]+double(wb)*(dot<0?-b.value[c]:b.value[c]);
                }
                o.qz=float(loop.base[3]*value[2]+loop.base[2]*value[3]);
                o.qw=float(loop.base[3]*value[3]-loop.base[2]*value[2]);
            } else {
                for(unsigned c=0;c<4;++c) {
                    const double delta=(b.value[c]-a.value[c])*double(fraction);
                    value[c]=loop.base[c]+a.value[c]+(loop.kind==6?double(static_cast<int>(delta+.5)):delta);
                }
                if(loop.target==0)for(unsigned c=0;c<4;++c)o.color[c]=unsigned(std::max(0.,std::min(255.,value[c])));
                else if(loop.target==7){o.x=float(value[0]);o.y=float(value[1]);o.z=float(value[2]);}
                else {o.w=float(value[0]);o.h=float(value[1]);}
            }
        }
        const auto active=active_states.find(o.id);
        if(active!=active_states.end())for(const auto& track:o.states[active->second.state].tracks) {
            const unsigned tick=active->second.settled?track.duration:
                unsigned(std::min<std::uint64_t>(track.duration,ui_ticks-active->second.start));
            std::array<double,4> value=track.base;
            if(!track.keys.empty()) {
                auto next=std::lower_bound(track.keys.begin(),track.keys.end(),int(tick),[](const UiKey& k,int t){return k.tick<t;});
                const auto& b=next==track.keys.end()?track.keys.back():*next;
                const auto& a=next==track.keys.begin()?b:next==track.keys.end()?b:*(next-1);
                const double fraction=b.tick>a.tick?double(int(tick)-a.tick)/(b.tick-a.tick):1;
                for(unsigned c=0;c<4;++c) {
                    const double delta=(b.value[c]-a.value[c])*fraction;
                    value[c]+=a.value[c]+(track.kind==6?double(static_cast<int>(delta+.5)):delta);
                }
            }
            if(track.target==0)for(unsigned c=0;c<4;++c)o.color[c]=unsigned(std::max(0.,std::min(255.,value[c])));
            else if(track.target==7){o.x=float(value[0]);o.y=float(value[1]);o.z=float(value[2]);}
            else {o.w=float(value[0]);o.h=float(value[1]);}
        }
        return o;
    }
    Transform transform(const Object& o,unsigned depth=0) const {
        if(depth>32) throw std::runtime_error("frontend parent cycle");
        Transform parent;
        if(o.parent) {
            const auto it=std::find_if(objects.begin(),objects.end(),[&](const Object& p){return p.id==o.parent;});
            if(it==objects.end()) throw std::runtime_error("frontend parent absent");
            parent=transform(*it,depth+1);
        }
        const auto current=sample(o);
        return combine(parent,current,current.type==2 || current.type==5);
    }
    std::array<unsigned,4> parent_color(const Object& o,std::array<unsigned,4> color,unsigned depth=0) const {
        if(!temporal || !o.parent)return color;
        if(depth>32)throw std::runtime_error("frontend parent color cycle");
        const auto it=std::find_if(objects.begin(),objects.end(),[&](const Object& p){return p.id==o.parent;});
        if(it==objects.end())throw std::runtime_error("frontend color parent absent");
        const auto parent=sample(*it);
        const auto effective=parent_color(parent,parent.color,depth+1);
        // Genuine4F1810..4F1D76 parent channel multiplication before /2 pack.
        for(unsigned c=0;c<4;++c)color[c]=(color[c]*effective[c]+128)>>8;
        return color;
    }
    bool visible(const Object& o,unsigned selected,unsigned depth=0) const {
        if(depth>32) return false;
        if(!temporal && o.name.rfind("hightlight",0)==0 && o.type==5) {
            // Native Highlight event target IDs: UG->df300d, QuickRace->ee300d.
            const unsigned wanted=selected==0?0xd30df:0xd30ee;
            if(o.id!=wanted) return false;
        }
        if(o.name.rfind("MenuBack_",0)==0 || o.name=="arrow" || o.name=="next_tag" || o.name=="Group(105)" || o.name=="Help" || o.name=="Group(52)" || o.name=="Group(53)") return false;
        if(o.parent) for(const auto& parent:objects) if(parent.id==o.parent) return visible(parent,selected,depth+1);
        return true;
    }
    void text(Canvas target,const Object& o,const Transform& m,std::string value,
              std::array<unsigned,4> color,unsigned resource) const {
        const auto& font=resource==8?small:large;
        const unsigned hash=resource==8?small_hash:large_hash;
        const auto tex=textures.find(hash);if(tex==textures.end()) return;
        float width=0;unsigned previous=0;
        for(unsigned char code:value) for(const auto& g:font.glyphs) if(g.code==code) {
            for(const auto& k:g.kerning) if(k.previous==previous) width+=float(k.adjustment);
            width+=float(g.advance);previous=code;break;
        }
        float x=o.justification&1?-width/2:o.justification&2?-width:0;
        const float y=o.justification&4?-font.line_height/2:o.justification&8?-font.line_height:0;
        previous=0;
        for(unsigned char code:value) for(const auto& g:font.glyphs) if(g.code==code) {
            for(const auto& k:g.kerning) if(k.previous==previous) x+=float(k.adjustment);
            const float tw=float(tex->second.width),th=float(tex->second.height);
            quad(target,tex->second,m,x+float(g.bearing_x),y+float(g.bearing_y),
                float(g.width),float(g.height),
                {{(float(g.x)-.5F)/tw,(float(g.y)-.5F)/th,(float(g.x)+g.width+.5F)/tw,(float(g.y)+g.height+.5F)/th}},color);
            x+=float(g.advance);previous=code;break;
        }
    }
};
FrontendArt::FrontendArt():impl_(std::make_unique<Impl>()) {}
FrontendArt::~FrontendArt()=default;
bool FrontendArt::load(const std::filesystem::path& root,std::string& error,bool background) {
    impl_=std::make_unique<Impl>();auto& out=*impl_;
    try {
        std::vector<unsigned char> front,global,language,font_global,language_textures;
        std::vector<RaceTableIssue> issues;RaceTableLimits limits;
        limits.max_file_size=8*1024*1024;limits.max_image_size=16*1024*1024;
        const auto read_image=[&](const char* name,std::vector<unsigned char>& bytes) {
            issues.clear();if(!load_race_image(root/name,bytes,issues,limits)) throw std::runtime_error(std::string(name)+": unreadable frontend input");
        };
        read_image("FrontEnd/FrontB.lzc",front);
        if(sha256_hex(sha256(front.data(),front.size()))!="94A714D1E8E3BEF7F77AEDCEE4FDC6350E285B06059B92D392687710E64E5D77") throw std::runtime_error("frontend identity mismatch");
        // The exact installed MU_Main_PC compressed package, authenticated by
        // its enclosing image. Decompression is bounded by load_race_image's
        // shared in-memory image decoder.
        constexpr std::size_t off=0xbec98c;
        if(front.size()<off+16 || std::memcmp(front.data()+off,"JDLZ",4)) throw std::runtime_error("main menu FNG absent");
        std::vector<unsigned char> fng;std::size_t unused=0;
        if(!jdlz_decompress(front.data()+off,dword(front.data()+off+12),16*1024*1024,fng,unused,error)) throw std::runtime_error("main menu FNG decode failed");
        for(std::size_t pos=0;pos+16<=fng.size();++pos) {
            if(std::memcmp(fng.data()+pos,"FOb\xEA",4)) continue;
            const auto n=dword(fng.data()+pos+12);
            if(std::memcmp(fng.data()+pos+8,"ObjD",4) || n>fng.size()-pos-16) throw std::runtime_error("invalid FNG object");
            Object o;std::size_t at=pos+16,end=at+n;
            while(at<end) {
                if(end-at<4) throw std::runtime_error("truncated FNG field");
                const auto* tag=fng.data()+at;const auto size=word(tag+2);const auto* v=tag+4;
                if(size>end-at-4) throw std::runtime_error("FNG field outside object");
                if(!std::memcmp(tag,"Ot",2) && size==4) o.type=dword(v);
                else if(!std::memcmp(tag,"On",2)) o.name=ascii(v,size);
                else if(!std::memcmp(tag,"OP",2) && size==16) {o.id=dword(v);o.resource=word(v+12);}
                else if(!std::memcmp(tag,"PA",2) && size==4) o.parent=dword(v);
                else if(!std::memcmp(tag,"Sj",2) && size==4) o.justification=dword(v);
                else if(!std::memcmp(tag,"St",2)) {for(unsigned i=0;i+1<size && word(v+i);i+=2) {if(word(v+i)>126) throw std::runtime_error("non-ASCII menu label");o.text+=char(word(v+i));}}
                else if(!std::memcmp(tag,"SA",2) && size>=64) {
                    for(unsigned i=0;i<4;++i) o.color[i]=dword(v+i*4);
                    o.x=real(v+28);o.y=real(v+32);o.z=real(v+36);o.qz=real(v+48);o.qw=real(v+52);
                    o.w=real(v+56);o.h=real(v+60);
                    if(size>=84) for(unsigned i=0;i<4;++i) o.uv[i]=real(v+68+i*4);
                    if(o.type==9) {
                        if(size<148) throw std::runtime_error("original colored backing corners absent");
                        for(unsigned corner=0;corner<4;++corner) for(unsigned c=0;c<4;++c)
                            o.corner_colors[corner][c]=dword(v+84+corner*16+c*4);
                    }
                }
                at+=4+size;
            }
            if(o.id==0xa17c) {
                // MU_Main_PC MenuScreen Init Kd origin, final delta zero.
                o.x=-5.6F;o.y=-111.155F;o.z=118.75F;
            }
            const std::size_t outer=dword(fng.data()+pos+4);
            if(outer<n+8 || outer>fng.size()-pos-8)throw std::runtime_error("frontend object script package invalid");
            load_ui_loops(fng.data()+pos+16+n,outer-n-8,o);
            load_ui_states(fng.data()+pos+16+n,outer-n-8,o);
            out.objects.push_back(std::move(o));pos+=15+n;
        }
        if(out.objects.size()!=161) throw std::runtime_error("main menu object count changed");
        read_image("Global/GLOBALB.BUN",global);
        if(sha256_hex(sha256(global.data(),global.size()))!="39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9") throw std::runtime_error("original global frontend identity mismatch");
        read_image("Global/GLOBALA.BUN",font_global);
        read_image("Languages/English.bin",language);read_image("Languages/LanguageTextures.bin",language_textures);
        if(sha256_hex(sha256(font_global.data(),font_global.size()))!="1502D9305F37E51503D4C4E57CAE32890059497AC450FEEE6D72B50323568953") throw std::runtime_error("original global font identity mismatch");
        const auto font=[&](const std::vector<unsigned char>& bytes,const char* name,RaceLapHudFont& f,unsigned& hash) {
            for(std::size_t at=0;at+8<=bytes.size();) {
                const auto size=dword(bytes.data()+at+4);
                if(size>bytes.size()-at-8) throw std::runtime_error("invalid font chunk");
                if(dword(bytes.data()+at)==0x30201 && size>=640 && ascii(bytes.data()+at+8,256)==name) {
                    const auto* header=bytes.data()+at+8+512;
                    auto texture_name=ascii(bytes.data()+at+8+256,256);
                    for(auto& c:texture_name) if(c>='a' && c<='z') c=char(c-32);
                    hash=bin_hash(texture_name);
                    const float height=float(header[18]+header[19]);
                    if(!decode_lap_hud_fntf(header,size-512,hash,height,0,1,f)) throw std::runtime_error("invalid original menu font");
                    return;
                }at+=8+size;
            }throw std::runtime_error("original menu font absent");
        };
        font(font_global,"conduitmditc_tt21i",out.large,out.large_hash);
        font(language,"conduitmditc_tt14i",out.small,out.small_hash);
        for(unsigned hash:{out.large_hash,out.small_hash,0xe2a5c626U,0x1abedeb1U,0x1f3bdb88U,0x868a1fcfU,bin_hash("NFSU_LOGO"),bin_hash("ARROW3"),bin_hash("CIRCLE"),0x5ed3f261U}) {
            Image image;
            if(!texture(front,hash,image,error) && !texture(global,hash,image,error) && !texture(font_global,hash,image,error) && !texture(language_textures,hash,image,error)) throw std::runtime_error("original frontend sprite/font atlas absent hash="+std::to_string(hash));
            out.textures.emplace(hash,std::move(image));
        }
        out.atlas.width=2048;
        unsigned x=0,y=0,row_height=0;
        for(auto& item:out.textures) {
            auto& image=item.second;
            if(image.width+2>out.atlas.width)throw std::runtime_error("frontend atlas texture exceeds width");
            if(x+image.width+2>out.atlas.width){x=0;y+=row_height;row_height=0;}
            image.atlas_x=x+1;image.atlas_y=y+1;
            x+=image.width+2;row_height=std::max(row_height,image.height+2);
        }
        out.atlas.height=1;while(out.atlas.height<y+row_height)out.atlas.height*=2;
        if(out.atlas.height>4096)throw std::runtime_error("frontend atlas exceeds resident budget");
        out.atlas.rgba.assign(std::size_t(out.atlas.width)*out.atlas.height*4,0);
        for(const auto& item:out.textures) {
            const auto& image=item.second;
            for(int row=-1;row<=int(image.height);++row)for(int column=-1;column<=int(image.width);++column) {
                const auto sy=std::max(0,std::min(int(image.height)-1,row));
                const auto sx=std::max(0,std::min(int(image.width)-1,column));
                const auto target=(std::size_t(int(image.atlas_y)+row)*out.atlas.width+int(image.atlas_x)+column)*4;
                std::copy_n(image.rgba.data()+(std::size_t(sy)*image.width+sx)*4,4,out.atlas.rgba.data()+target);
            }
        }
        out.background.width=1280;out.background.height=720;
        if(background && !load_frontend_scene(root,out.background.rgba,1280,720,error)) return false;
        error.clear();return true;
    } catch(const std::exception& e) {error=e.what();return false;}
}
void FrontendArt::draw(ScreenImage image,const FrontendMenu& menu) const {
    const auto& p=*impl_;
    if(!image.pixels || image.stride<std::size_t(image.width)*4 || p.background.rgba.empty())return;
    for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x)
        std::copy_n(p.background.rgba.data()+((std::size_t(y)*p.background.height/image.height)*p.background.width+std::size_t(x)*p.background.width/image.width)*4,4,image.pixels+y*image.stride+x*4);
    draw_overlay(image,menu);
}
void FrontendArt::draw_overlay(ScreenImage image,const FrontendMenu& menu) const {
    if(!image.pixels || image.stride<std::size_t(image.width)*4)return;
    impl_->draw(Canvas{image},menu);
}
void FrontendArt::advance(float seconds) {
    if(!std::isfinite(seconds) || seconds<0 || seconds>60)throw std::runtime_error("invalid frontend UI time step");
    // 4F5CF0 multiplies original frame seconds73AD38 by1200 then truncates.
    if(!impl_->temporal) {
        for(const auto& object:impl_->objects)if(object.type==2)
            impl_->start_named_state(object,"Init",0);
    }
    impl_->temporal=true;impl_->ui_ticks+=static_cast<unsigned>(double(seconds)*1200);
}
const FrontendUiAtlas& FrontendArt::atlas() const {return impl_->atlas;}
void FrontendArt::record_ui(unsigned width,unsigned height,const FrontendMenu& menu,FrontendUiFrame& frame) const {
    frame.vertices.clear();frame.additive.clear();
    frame.vertices.reserve(kFrontendUiMaxQuads*4);frame.additive.reserve(kFrontendUiMaxQuads);
    impl_->draw(Canvas{width,height,impl_->atlas,frame},menu);
}
void FrontendArt::Impl::draw(Canvas image,const FrontendMenu& menu) const {
    const auto& p=*this;
    p.select(menu.selected());
    std::vector<std::size_t> order(p.objects.size());for(std::size_t i=0;i<order.size();++i) order[i]=i;
    std::stable_sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b) {return p.transform(p.objects[a]).z>p.transform(p.objects[b]).z;});
    const unsigned hashes[]={0xe2a5c626,0x1f3bdb88,0x1abedeb1,0,bin_hash("ARROW3"),bin_hash("CIRCLE"),0x868a1fcf,bin_hash("NFSU_LOGO")};
    for(auto i:order) {
        const auto o=p.sample(p.objects[i]);if(!p.visible(o,menu.selected()) || o.type==5 || o.name=="NFSU_logo") continue;
        auto color=o.color;
        // Original steady states from UnHighlight/Highlight, after Init fades.
        if(!p.temporal && !color[3] && (o.type==2 || o.type==5)) color[3]=255;
        if(!color[3]) continue;
        const auto m=p.transform(p.objects[i]);
        if((o.type==1 || o.type==9) && m.y>150) continue;
        if(o.type==2) {
            if(o.resource!=3 && o.resource!=8) continue;
            std::string label=o.text;
            const bool underground=o.name=="UndergroundMode",free=o.name=="QuickRace";
            if(free) label="FREE DRIVING";
            if(o.name=="Customize") label="CUSTOMIZE RIDE";
            if(o.name=="Description") label=menu.selected()==0?"Begin your Underground career":"Drive the first track. + returns to the menu.";
            if((underground || free) && !p.temporal) color=(menu.selected()==(free?1U:0U))?std::array<unsigned,4>{{242,233,207,255}}:std::array<unsigned,4>{{180,154,84,255}};
            else if(o.name=="STATISTICS" || o.name=="Options" || o.name=="Customize" || o.name=="Online")
                color={{110,110,110,p.temporal?(color[3]*180+127)/255:180}};
            color=p.parent_color(o,color);
            p.text(image,o,m,label,color,o.resource);
        } else if((o.type==1 || o.type==9) && o.resource<8) {
            const auto tex=p.textures.find(hashes[o.resource]);if(tex==p.textures.end()) continue;
            color=p.parent_color(o,color);
            auto corners=o.corner_colors;
            if(o.type==9)for(auto& corner:corners)corner=p.parent_color(o,corner);
            quad(image,tex->second,m,-o.w/2,-o.h/2,o.w,o.h,o.uv,color,o.type==9?&corners:nullptr);
        }
    }
    // The identity logo is the final foreground image over its angular backing.
    for(const auto& o:p.objects) if(o.name=="NFSU_logo") {
        const auto tex=p.textures.find(bin_hash("NFSU_LOGO"));
        if(tex!=p.textures.end()) quad(image,tex->second,p.transform(o),
            -o.w/2,-o.h/2,o.w,o.h,o.uv,o.color);
    }
    // Chyron.fng's original EA logo, 276x58 backing and proportional font.
    // Anchor its authored 640x480 banner inside the Switch safe area; no
    // desktop or Steam overlay is part of the game UI.
    const auto white=p.textures.find(0xe2a5c626U);
    const auto logo=p.textures.find(0x5ed3f261U);
    Transform bar;bar.x=-138;bar.y=171;
    if(white!=p.textures.end()) quad(image,white->second,bar,-138,-29,276,58,{{0,0,1,1}},{{192,128,0,230}});
    Transform badge;badge.x=-269;badge.y=171;
    if(logo!=p.textures.end()) quad(image,logo->second,badge,-31,-31,62,62,{{0,0,1,1}},{{255,255,255,255}});
    Object help;help.justification=0;
    Transform banner;banner.x=-228;banner.y=148;banner.a=.7F;banner.d=.7F;
    p.text(image,help,banner,menu.artist(),{{242,233,207,255}},3);
    banner.y+=15;banner.a=.6F;banner.d=.6F;
    p.text(image,help,banner,"\""+menu.title()+"\"",{{242,233,207,255}},3);
    banner.y+=14;p.text(image,help,banner,menu.album(),{{242,233,207,255}},3);
    Transform footer;footer.x=-266;footer.y=219;
    p.text(image,help,footer,"A: Enter   B: Exit    Other modes locked",{{242,233,207,255}},8);
    if(!menu.notice().empty()) {
        if(image.gpu) {
            Transform notice;notice.x=-266;notice.y=-218;notice.a=.6F;notice.d=.6F;
            p.text(image,help,notice,menu.notice().substr(0,100),{{255,210,120,255}},8);
        } else {
            fill_screen_rect(image,0,0,image.width,35,{0,0,0,220});
            draw_screen_text(image,12,8,menu.notice().substr(0,100),2,{255,210,120,255},{0,0,0,255});
        }
    }
}
}
