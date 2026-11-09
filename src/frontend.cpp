#include "nfsu/frontend.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/career_garage.h"
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
#include <functional>
#include <stdexcept>
namespace nfsu {
FrontendAction FrontendMenu::update(FrontendInput input) {
    if(input.profiles) return FrontendAction::profiles;
    if(input.back) return FrontendAction::exit;
    if(input.up!=input.down) {
        const unsigned count=2+unsigned(multiplayer_)+unsigned(options_);
        selected_=(selected_+(input.up?count-1:1))%count;
        notice_.clear();
    }
    if(!input.accept) return FrontendAction::none;
    if(options_&&selected_==2)return FrontendAction::options;
    if(multiplayer_&&selected_==multiplayer_selection())return FrontendAction::multiplayer;
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
struct Image {unsigned width=0,height=0,atlas_x=0,atlas_y=0;std::vector<unsigned char> rgba;TextureAlphaBlend blend=TextureAlphaBlend::source_over;bool font=false;};
struct UiKey {int tick=0;std::array<double,4> value{};};
struct UiLoop {unsigned kind=0,target=0,duration=0;std::array<double,4> base{};std::vector<UiKey> keys;};
struct UiState {std::string name;std::vector<UiLoop> tracks;std::vector<std::array<unsigned,3>> events;};
struct Object {
    unsigned id=0,parent=0,type=0,resource=0,justification=0,language=0,wrap_width=0;bool chyron=false;
    std::string name,text;
    std::vector<UiLoop> loops;
    std::vector<UiState> states;
    float x=0,y=0,z=0,w=1,h=1,qx=0,qy=0,qz=0,qw=1;
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
            bool init=false,loop=false,looping_frame=true;UiLoop track;
            for(std::size_t field=at+8;field<at+8+n;) {
                if(at+8+n-field<4)throw std::runtime_error("frontend script field truncated");
                const auto* tag=data+field;const unsigned size=word(tag+2);const auto* v=tag+4;
                if(size>at+8+n-field-4)throw std::runtime_error("frontend script field outside chunk");
                if(!std::memcmp(tag,"Sn",2))init=ascii(v,size)=="Init";
                if(!std::memcmp(tag,"Sh",2) && size==16)loop=dword(v+8)==1;
                if(!std::memcmp(tag,"FI",2) && size==8) {
                    track=UiLoop{};track.kind=v[0];track.duration=dword(v+4)&0xffffff;
                    // Common Reputation Init also contains one static position FI.
                    // Its nonloop setup was applied by post_state; animate only
                    // the timed RGBA/scale frames in the same authored script.
                    looping_frame=!(track.kind==4&&v[1]==12&&v[2]==1&&v[3]==0&&track.duration==0);
                    if(init && loop && looping_frame && (v[1]!=(track.kind==4?12:16) || v[2]!=1 || v[3]!=1))
                        throw std::runtime_error("unsupported native frontend loop frame");
                }
                if(!std::memcmp(tag,"To",2) && size==4)track.target=dword(v);
                if(!std::memcmp(tag,"Kd",2) && init && loop && looping_frame) {
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
            UiState state;UiLoop track;bool nonloop=false;unsigned state_duration=0;
            for(std::size_t field=at+8;field<at+8+n;) {
                if(at+8+n-field<4)throw std::runtime_error("frontend state field truncated");
                const auto* tag=data+field;const unsigned size=word(tag+2);const auto* v=tag+4;
                if(size>at+8+n-field-4)throw std::runtime_error("frontend state field outside chunk");
                if(!std::memcmp(tag,"Sn",2))state.name=ascii(v,size);
                const bool selection=state.name=="HighLight" || state.name=="UnHighLight";
                const bool text_entry=object.type==2 && (state.name=="Init" || state.name=="FadeIn");
                const bool retained=selection || text_entry || (object.chyron && (state.name=="Init" || state.name=="Show" || state.name=="Hide"));
                if(!std::memcmp(tag,"Sh",2) && size==16){nonloop=dword(v+8)==0;state_duration=dword(v+4);}
                if(!std::memcmp(tag,"FI",2) && size==8) {
                    track=UiLoop{};track.kind=v[0];track.duration=dword(v+4)&0xffffff;
                    if(object.chyron && !track.duration)track.duration=state_duration;
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
               (object.type==2 && (state.name=="Init" || state.name=="FadeIn")) ||
               (object.chyron && (state.name=="Init" || state.name=="Show" || state.name=="Hide")))object.states.push_back(std::move(state));
        }
        at+=n+8;
    }
}
Object decode_ui_object(const std::vector<unsigned char>& fng,std::size_t pos,bool chyron=false) {
            const auto n=dword(fng.data()+pos+12);
            if(std::memcmp(fng.data()+pos+8,"ObjD",4) || n>fng.size()-pos-16) throw std::runtime_error("invalid FNG object");
            Object o;o.chyron=chyron;std::size_t at=pos+16,end=at+n;
            while(at<end) {
                if(end-at<4) throw std::runtime_error("truncated FNG field");
                const auto* tag=fng.data()+at;const auto size=word(tag+2);const auto* v=tag+4;
                if(size>end-at-4) throw std::runtime_error("FNG field outside object");
                if(!std::memcmp(tag,"Ot",2) && size==4) o.type=dword(v);
                else if(!std::memcmp(tag,"On",2)) o.name=ascii(v,size);
                else if(!std::memcmp(tag,"OP",2) && size==16) {o.id=dword(v);o.resource=word(v+12);}
                else if(!std::memcmp(tag,"PA",2) && size==4) o.parent=dword(v);
                else if(!std::memcmp(tag,"SH",2) && size==4) o.language=dword(v);
                else if(!std::memcmp(tag,"Sj",2) && size==4) o.justification=dword(v);
                else if(!std::memcmp(tag,"Sw",2) && size==4) o.wrap_width=dword(v);
                else if(!std::memcmp(tag,"St",2)) {for(unsigned i=0;i+1<size && word(v+i);i+=2) {if(word(v+i)>126) throw std::runtime_error("non-ASCII menu label");o.text+=char(word(v+i));}}
                else if(!std::memcmp(tag,"SA",2) && size>=64) {
                    for(unsigned i=0;i<4;++i) o.color[i]=dword(v+i*4);
                    o.x=real(v+28);o.y=real(v+32);o.z=real(v+36);o.qx=real(v+40);o.qy=real(v+44);o.qz=real(v+48);o.qw=real(v+52);
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
    return o;
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
    // Keep the blend just inside a destination pixel to preserve thin stems.
    const float footprint_u=tex.font?std::clamp(.8F*std::abs((uv[2]-uv[0])*tex.width/(det*w))*(std::abs(m.d)/sx+std::abs(m.c)/sy),.001F,1.F):1.F;
    const float footprint_v=tex.font?std::clamp(.8F*std::abs((uv[3]-uv[1])*tex.height/(det*h))*(std::abs(m.b)/sx+std::abs(m.a)/sy),.001F,1.F):1.F;
    const float xs[4]={x,x+w,x+w,x},ys[4]={y,y,y+h,y+h};
    if(target.gpu) {
        auto& frame=*target.gpu;
        if(frame.additive.size()>=kFrontendUiMaxQuads)throw std::runtime_error("frontend GPU quad capacity exceeded");
        const float us[4]={uv[0],uv[2],uv[2],uv[0]},vs[4]={uv[1],uv[1],uv[3],uv[3]};
        const auto font_footprint=(unsigned(footprint_u*1023+.5F)<<10)|unsigned(footprint_v*1023+.5F);
        for(unsigned i=0;i<4;++i) {
            TrackVertex vertex;
            vertex.px=(320+m.x+m.a*xs[i]+m.c*ys[i])*sx;
            vertex.py=(240+m.y+m.b*xs[i]+m.d*ys[i])*sy;
            vertex.pz=tex.font?kFrontendFontVertexZ-float(font_footprint):-1.F;
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
        std::array<unsigned char,4> filtered{};
        if(tex.font) {
            const float ax=tu*tex.width-.5F,ay=tv*tex.height-.5F;
            const float x0=std::floor(ax),y0=std::floor(ay);
            const float fx=std::clamp((ax-x0-.5F)/footprint_u+.5F,0.F,1.F);
            const float fy=std::clamp((ay-y0-.5F)/footprint_v+.5F,0.F,1.F);
            const auto pixel=[&](int x,int y) {
                return tex.rgba.data()+4*(std::size_t(std::clamp(y,0,int(tex.height)-1))*tex.width+
                    std::clamp(x,0,int(tex.width)-1));
            };
            const auto* tl=pixel(int(x0),int(y0));const auto* tr=pixel(int(x0)+1,int(y0));
            const auto* bl=pixel(int(x0),int(y0)+1);const auto* br=pixel(int(x0)+1,int(y0)+1);
            for(unsigned c=0;c<4;++c)filtered[c]=static_cast<unsigned char>(.5F+
                (tl[c]*(1-fx)+tr[c]*fx)*(1-fy)+(bl[c]*(1-fx)+br[c]*fx)*fy);
            filtered[3]=static_cast<unsigned char>(std::clamp(1.25F*(filtered[3]-127.5F)+128.F,0.F,255.F));
            src=filtered.data();
        }
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
struct RaceLoadingArt::Impl {
    std::array<Image,3> textures;
    std::array<Object,3> sprites;
    Object group;
    std::vector<unsigned char> background;
    unsigned cached_width=0,cached_height=0;
    bool ready=false;
};
RaceLoadingArt::RaceLoadingArt():impl_(std::make_unique<Impl>()){}
RaceLoadingArt::~RaceLoadingArt()=default;
bool RaceLoadingArt::load(const std::filesystem::path& root,std::string& error) {
    impl_=std::make_unique<Impl>();auto& out=*impl_;
    try {
        std::vector<unsigned char> global,a;std::vector<RaceTableIssue> issues;
        RaceTableLimits limits;limits.max_file_size=32*1024*1024;limits.max_image_size=32*1024*1024;
        if(!load_race_image(root/"Global/GLOBALB.BUN",global,issues,limits) ||
           sha256_hex(sha256(global.data(),global.size()))!="39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9")
            throw std::runtime_error("loading artwork global identity mismatch");
        if(!load_race_image(root/"Global/GLOBALA.BUN",a,issues,limits) ||
           sha256_hex(sha256(a.data(),a.size()))!="1502D9305F37E51503D4C4E57CAE32890059497AC450FEEE6D72B50323568953")
            throw std::runtime_error("loading sprite pack identity mismatch");
        const char* names[]={"PC_LOAD","CHROMEGENERIC_LOADINGBAR","GENERIC_LOADING_BAR2"};
        for(unsigned i=0;i<3;++i)if(!texture(global,bin_hash(names[i]),out.textures[i],error) &&
                                   !texture(a,bin_hash(names[i]),out.textures[i],error))
            throw std::runtime_error(std::string("loading sprite absent: ")+names[i]);
        // PC_Loading.fng in authenticated GLOBALB. Only the generic branch:
        // no PC keyboard/controller hints or unrelated random tip is invented.
        const auto first=global.begin()+0x230208;
        std::vector<unsigned char> fng(first,first+8+0x56a8);
        unsigned found=0;
        for(std::size_t pos=0;pos+16<=fng.size();++pos) {
            if(std::memcmp(fng.data()+pos,"FOb\xEA",4))continue;
            const auto n=dword(fng.data()+pos+12),outer=dword(fng.data()+pos+4);
            if(n>fng.size()-pos-16 || outer>fng.size()-pos-8)throw std::runtime_error("loading object bounds");
            // Copy just ObjD: generic_loading_bar's zero-duration Init chains
            // to Move; the generic main-menu loop decoder is not its controller.
            std::vector<unsigned char> object(fng.begin()+pos,fng.begin()+pos+16+n);
            const unsigned clipped=n+8;std::memcpy(object.data()+4,&clipped,4);
            const auto o=decode_ui_object(object,0);
            if(o.name=="Generic_Loading_Group"){out.group=o;found|=1;}
            for(unsigned i=0;i<3;++i)if(o.name==(i==0?"PC_load":i==1?"ChromeGeneric_LoadingBar":"generic_loading_bar")) {
                out.sprites[i]=o;found|=2U<<i;
            }
            pos+=7+outer;
        }
        if(found!=15)throw std::runtime_error("original generic loading objects absent");
        out.ready=true;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void RaceLoadingArt::draw(ScreenImage image,double seconds,bool cached)const {
    auto& p=*impl_;
    if(!p.ready || !image.pixels || image.stride<std::size_t(image.width)*4)return;
    const auto parent=combine({},p.group,true);
    const std::size_t row_bytes=std::size_t(image.width)*4;
    const auto background=[&](ScreenImage base) {
        fill_screen_rect(base,0,0,base.width,base.height,{0,0,0,255});
        for(unsigned i=0;i<2;++i) {
            const auto& o=p.sprites[i];const auto m=combine(i?parent:Transform{},o,false);
            quad(Canvas{base},p.textures[i],m,-o.w/2,-o.h/2,o.w,o.h,o.uv,o.color);
        }
    };
    // Loading polls can be much more frequent than frames. Rasterize the two
    // static retail sprites once per output size, then only animate the bar.
    // The cache is released with loading artwork before race GPU upload.
    if(!cached)background(image);
    else {
        if(p.cached_width!=image.width || p.cached_height!=image.height || p.background.empty()) {
            p.background.resize(row_bytes*image.height);
            background({p.background.data(),image.width,image.height,row_bytes});
            p.cached_width=image.width;p.cached_height=image.height;
        }
        for(unsigned y=0;y<image.height;++y)
            std::memcpy(image.pixels+std::size_t(y)*image.stride,p.background.data()+std::size_t(y)*row_bytes,row_bytes);
    }
    auto o=p.sprites[2];
    // Authored Move FI/Kd (1200Hz,1600-tick repeat), chained by Init.
    // Activity indicator, not a percentage of file loading completion.
    const unsigned tick=std::isfinite(seconds)&&seconds>0?
        static_cast<unsigned>(std::fmod(seconds*1200,1600)):0;
    const unsigned slot=std::min(3U,tick/400),local=tick%400;
    o.x=-37.F+37.F*float(slot)+(slot<3&&local>380?37.F*float(local-380)/20.F:0.F);o.z=-1;
    const auto m=combine(parent,o,false);
    quad(Canvas{image},p.textures[2],m,-o.w/2,-o.h/2,o.w,o.h,o.uv,o.color);
}

struct DialogArt {
    std::vector<Object> objects;
    std::map<unsigned,std::vector<unsigned char>> scripts;
    std::vector<unsigned> resources;
};
struct FrontendArt::Impl {
    Image background;
    FrontendUiAtlas atlas;
    void draw(Canvas image,const FrontendMenu& menu) const;
    std::map<unsigned,Image> textures;
    std::vector<Object> objects;
    std::array<std::vector<Object>,5> post_objects;
    std::array<std::map<unsigned,std::vector<unsigned char>>,5> post_scripts;
    mutable std::uint64_t post_page_key=~std::uint64_t(0);
    mutable unsigned post_selected=~0U,post_old_selected=~0U;
    mutable std::uint64_t post_selected_started=0;
    mutable std::uint64_t post_started=0;
    std::vector<unsigned> style_thresholds;
    std::array<std::vector<Object>,5> career_objects;
    std::array<DialogArt,6> dialogs;
    mutable unsigned info_revision=~0U;mutable std::uint64_t info_started=0;
    void draw_dialog(Canvas,unsigned,const PostRaceValues&,unsigned,unsigned,unsigned fade_out=~0U)const;
    mutable unsigned dialog_key=~0U;mutable std::uint64_t dialog_started=0;
    std::vector<Object> welcome_objects;
    std::map<unsigned,std::vector<unsigned char>> welcome_scripts;
    void draw_welcome(Canvas,bool,unsigned)const;
    std::vector<Object> garage_common,career_browser,career_scroller;
    struct GarageArt {std::vector<Object> objects;std::vector<unsigned> resources;};
    std::map<std::size_t,GarageArt> garage_pages;
    std::map<unsigned,std::string> career_titles,career_descriptions,career_track_names;
    std::array<std::array<std::string,4>,9> garage_package_contents;
    std::map<unsigned,unsigned> career_backgrounds;
    std::map<unsigned,unsigned> career_maps;
    std::map<unsigned,bool> career_sprint;
    void draw_magazine(Canvas,const CareerMagazineRecipe&,const std::string&,bool background)const;
    std::vector<Object> magazine_front,magazine_back;
    std::vector<unsigned> magazine_textures,magazine_mask_resources;
    void pack_atlas(bool compact=false);
    void draw_career(Canvas,const CareerCatalog&,const CareerProfile&,unsigned,const std::string&,bool controls=true,const CareerGarage* garage=nullptr)const;
    void draw_post_race(Canvas,const PostRaceMenu&,const PostRaceValues&)const;
    void draw_free_drive(Canvas,const CareerCatalog&,const CareerProfile&,unsigned,unsigned,const std::string&,const QuickRaceOptions*,bool)const;
    RaceLapHudFont large,small;
    unsigned large_hash=0,small_hash=0;
    std::uint64_t ui_ticks=0;bool temporal=false;
    double chyron_ticks=0;mutable FrontendChyron chyron;mutable unsigned song_revision=0;
    struct ActiveState {unsigned state=0;std::uint64_t start=0;bool settled=false;};
    mutable std::map<unsigned,ActiveState> active_states;
    mutable unsigned last_selection=~0U;
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
        const bool first=last_selection==~0U;
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

    struct SampledObject {std::uint64_t generation=0;Object pose;};
    mutable std::map<const Object*,SampledObject> sampled_objects;
    mutable std::uint64_t sample_generation=0;
    const Object& sample(const Object& original) const {
        if(!temporal && !original.chyron)return original;
        auto inserted=sampled_objects.try_emplace(&original);
        auto& cached=inserted.first->second;
        if(!inserted.second && cached.generation==sample_generation)return cached.pose;
        if(inserted.second)cached.pose=original;
        // Immutable authored scripts/labels stay resident. Only animated pose
        // fields reset each draw, before evaluating exactly the original keys.
        auto& o=cached.pose;o.color=original.color;
        o.x=original.x;o.y=original.y;o.z=original.z;
        o.w=original.w;o.h=original.h;o.qz=original.qz;o.qw=original.qw;
        cached.generation=sample_generation;
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
        const auto& current=sample(o);
        return combine(parent,current,current.type==2 || current.type==5);
    }
    std::array<unsigned,4> parent_color(const Object& o,std::array<unsigned,4> color,unsigned depth=0) const {
        if((!temporal && !o.chyron) || !o.parent)return color;
        if(depth>32)throw std::runtime_error("frontend parent color cycle");
        const auto it=std::find_if(objects.begin(),objects.end(),[&](const Object& p){return p.id==o.parent;});
        if(it==objects.end())throw std::runtime_error("frontend color parent absent");
        const auto& parent=sample(*it);
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
            if(selected==2 || o.id!=wanted) return false;
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
        if(o.wrap_width) {
            const auto measure=[&](const std::string& line) {
                float width=0;unsigned previous=0;
                for(unsigned char code:line)for(const auto& g:font.glyphs)if(g.code==code) {
                    for(const auto& k:g.kerning)if(k.previous==previous)width+=float(k.adjustment);
                    width+=float(g.advance);previous=code;break;
                }
                return width;
            };
            std::vector<std::string> lines;std::string line,word;
            const auto emit=[&]() {if(!word.empty()) {
                const auto trial=line.empty()?word:line+" "+word;
                if(!line.empty()&&measure(trial)>o.wrap_width){lines.push_back(line);line=word;}
                else line=trial;
                word.clear();
            }};
            for(char c:value) {
                if(c==' '||c=='\n'){emit();if(c=='\n'){lines.push_back(line);line.clear();}}
                else word+=c;
            }
            emit();if(!line.empty())lines.push_back(line);
            auto plain=o;plain.wrap_width=0;auto pose=m;
            for(const auto& l:lines){text(target,plain,pose,l,color,resource);pose.x+=m.c*font.line_height;pose.y+=m.d*font.line_height;}
            return;
        }
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
void FrontendArt::Impl::pack_atlas(bool compact) {
        atlas.width=2048;
        unsigned x=0,y=0,row_height=0;
        std::vector<Image*> images;for(auto& item:textures)images.push_back(&item.second);
        // Keep the accepted main-menu packing. Career adds large map planes;
        // group equal-height rows to avoid paying a map-height row for icons.
        if(compact)std::stable_sort(images.begin(),images.end(),[](const auto* a,const auto* b){
            return a->height!=b->height?a->height>b->height:a->width>b->width;
        });
        struct Shelf {unsigned y,height,x;};std::vector<Shelf> shelves;
        for(auto* entry:images) {
            auto& image=*entry;
            if(image.width+2>atlas.width)throw std::runtime_error("frontend atlas texture exceeds width");
            if(compact) {
                // Reuse earlier shelves' horizontal gaps. New garage icons
                // must fit the existing resident cap without reducing assets.
                auto best=shelves.end();
                for(auto at=shelves.begin();at!=shelves.end();++at)
                    if(at->height>=image.height+2&&at->x+image.width+2<=atlas.width&&
                       (best==shelves.end()||at->height<best->height||
                        (at->height==best->height&&at->x>best->x)))best=at;
                if(best==shelves.end()){shelves.push_back({y,image.height+2,0});y+=image.height+2;best=shelves.end()-1;}
                image.atlas_x=best->x+1;image.atlas_y=best->y+1;best->x+=image.width+2;
            }else {
                if(x+image.width+2>atlas.width){x=0;y+=row_height;row_height=0;}
                image.atlas_x=x+1;image.atlas_y=y+1;
                x+=image.width+2;row_height=std::max(row_height,image.height+2);
            }
        }
        // Deko textures support non-power-of-two extents. Career pages keep
        // exact row coverage rather than a large unused power-of-two tail.
        if(compact)atlas.height=(y+row_height+63U)&~63U;
        else {atlas.height=1;while(atlas.height<y+row_height)atlas.height*=2;}
        if(atlas.height>4096)throw std::runtime_error("frontend atlas exceeds resident budget");
        atlas.rgba.assign(std::size_t(atlas.width)*atlas.height*4,0);
        for(const auto& item:textures) {
            const auto& image=item.second;
            for(int row=-1;row<=int(image.height);++row)for(int column=-1;column<=int(image.width);++column) {
                const auto sy=std::max(0,std::min(int(image.height)-1,row));
                const auto sx=std::max(0,std::min(int(image.width)-1,column));
                const auto target=(std::size_t(int(image.atlas_y)+row)*atlas.width+int(image.atlas_x)+column)*4;
                std::copy_n(image.rgba.data()+(std::size_t(sy)*image.width+sx)*4,4,atlas.rgba.data()+target);
            }
        }
}
FrontendArt::FrontendArt():impl_(std::make_unique<Impl>()) {}
FrontendArt::~FrontendArt()=default;
bool FrontendArt::load(const std::filesystem::path& root,std::string& error,bool background) {
    return load_assets(root,error,background,false);
}
bool FrontendArt::load_startup(const std::filesystem::path& root,std::string& error) {
    return load_assets(root,error,false,true);
}
bool FrontendArt::load_assets(const std::filesystem::path& root,std::string& error,bool background,bool startup) {
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
            Object o=decode_ui_object(fng,pos);
            out.objects.push_back(std::move(o));pos+=15+n;
        }
        if(out.objects.size()!=161) throw std::runtime_error("main menu object count changed");
        read_image("Global/GLOBALB.BUN",global);
        if(sha256_hex(sha256(global.data(),global.size()))!="39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9") throw std::runtime_error("original global frontend identity mismatch");
        // Authenticated uncompressed Chyron.fng object package in GLOBALB.
        // Its resource list binds font0, EA logo1, White16x16=2,45Corner=3.
        for(std::size_t pos=0x23912c;pos<0x23b640;) {
            if(pos+16>global.size() || std::memcmp(global.data()+pos,"FOb\xEA",4))
                throw std::runtime_error("original Chyron object package absent");
            const auto outer=dword(global.data()+pos+4);
            out.objects.push_back(decode_ui_object(global,pos,true));pos+=8+outer;
        }
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
            image.font=hash==out.large_hash||hash==out.small_hash;
            out.textures.emplace(hash,std::move(image));
        }
        if(startup) {
            decode_welcome(front,language,global,error);
            decode_profiles(front,language,error);
        } else out.pack_atlas();
        out.background.width=1280;out.background.height=720;
        if(background && !load_frontend_scene(root,out.background.rgba,1280,720,error)) return false;
        error.clear();return true;
    } catch(const std::exception& e) {error=e.what();return false;}
}

namespace {
// A bounded FEng script sampler for the post-race package. Every retained
// channel is evaluated from its own base plus authored key delta; no guessed
// offscreen-to-onscreen offsets. A default tick requests the steady endpoint.
void post_state(Object& o,const unsigned char* p,std::size_t length,const char* wanted,unsigned tick=~0U) {
    for(std::size_t at=0;at+8<=length;) {
        const auto n=dword(p+at+4);if(n>length-at-8)throw std::runtime_error("post-race script bounds");
        if(!std::memcmp(p+at,"Scrp",4)) {
            std::string name;unsigned kind=0,target=0,duration=0;bool loop=false;
            for(std::size_t f=at+8;f<at+8+n;) {
                if(at+8+n-f<4)throw std::runtime_error("post-race field header");
                const auto size=word(p+f+2);const auto* v=p+f+4;
                if(size>at+8+n-f-4)throw std::runtime_error("post-race script field bounds");
                if(!std::memcmp(p+f,"Sn",2))name=ascii(v,size);
                if(!std::memcmp(p+f,"Sh",2) && size==16)loop=dword(v+8)==1;
                if(!std::memcmp(p+f,"FI",2) && size==8){kind=v[0];duration=dword(v+4)&0xffffff;}
                if(!std::memcmp(p+f,"To",2) && size==4)target=dword(v);
                if(name==wanted && !std::memcmp(p+f,"Kd",2)) {
                    const unsigned dims=kind==4?3:4,stride=4+4*dims;
                    if((kind!=4 && kind!=5 && kind!=6) || size<stride || size%stride || dword(v)!=0xffffffffU)
                        throw std::runtime_error("post-race state keys unsupported");
                    const unsigned sample_tick=tick!=~0U && loop && duration?tick%duration:tick;
                    if(kind==5) {
                        if(target!=10||real(v+4)!=0||real(v+8)!=0)throw std::runtime_error("post-race quaternion plane");
                        float z=0,w=1;
                        if(size>stride) {
                            unsigned a=stride,b=stride;
                            while(b+stride<size&&dword(v+b)<sample_tick){a=b;b+=stride;}
                            if(sample_tick>=dword(v+b))a=b;
                            const auto* qa=v+a+4;const auto* qb=v+b+4;
                            if(real(qa)!=0||real(qa+4)!=0||real(qb)!=0||real(qb+4)!=0)throw std::runtime_error("post-race quaternion key plane");
                            const unsigned ta=dword(v+a),tb=dword(v+b);
                            const double fraction=tb>ta?std::clamp((double(sample_tick)-ta)/(tb-ta),0.0,1.0):1;
                            if(a==b){z=real(qb+8);w=real(qb+12);}
                            else {
                                const double dot=double(real(qa+8))*real(qb+8)+double(real(qa+12))*real(qb+12);
                                if(std::abs(dot)>1e-5&&std::abs(std::abs(dot)-.70710678)>1e-5)throw std::runtime_error("post-race quaternion native angle");
                                const unsigned angle=std::abs(dot)<1e-5?16383:8191;
                                const auto denominator=frontend_fixed_sine(static_cast<std::uint16_t>(angle));
                                const auto wa=frontend_fixed_sine(static_cast<std::uint16_t>((1-fraction)*angle))/denominator;
                                const auto wb=frontend_fixed_sine(static_cast<std::uint16_t>(fraction*angle))/denominator;
                                z=wa*real(qa+8)+wb*(dot<0?-real(qb+8):real(qb+8));
                                w=wa*real(qa+12)+wb*(dot<0?-real(qb+12):real(qb+12));
                            }
                        }
                        o.qz=real(v+16)*z+real(v+12)*w;o.qw=real(v+16)*w-real(v+12)*z;
                        if(!std::isfinite(o.qz)||!std::isfinite(o.qw))throw std::runtime_error("post-race nonfinite quaternion");
                        f+=4+size;continue;
                    }
                    std::array<float,4> value{};
                    for(unsigned k=0;k<dims;++k) {
                        const auto read_key=[&](const unsigned char* a){return kind==6?float(static_cast<std::int32_t>(dword(a))):real(a);};
                        value[k]=read_key(v+4+4*k);
                        if(size>stride) {
                            unsigned a=stride,b=stride;
                            while(b+stride<size && dword(v+b)<sample_tick){a=b;b+=stride;}
                            if(sample_tick>=dword(v+b))a=b;
                            const auto ta=dword(v+a),tb=dword(v+b);
                            const double fraction=tb>ta?std::clamp((double(sample_tick)-ta)/(tb-ta),0.0,1.0):1;
                            const double delta=(read_key(v+b+4+4*k)-read_key(v+a+4+4*k))*fraction;
                            value[k]+=read_key(v+a+4+4*k)+float(kind==6?static_cast<int>(delta+.5):delta);
                        }
                        if(!std::isfinite(value[k]))throw std::runtime_error("post-race nonfinite state");
                    }
                    if(target==0)for(unsigned k=0;k<4;++k)o.color[k]=unsigned(std::clamp(value[k],0.F,255.F));
                    else if(target==7){o.x=value[0];o.y=value[1];o.z=value[2];}
                    else if(target==14){o.w=value[0];o.h=value[1];}
                    else throw std::runtime_error("post-race state target unsupported");
                }
                f+=4+size;
            }
        }
        at+=8+n;
    }
}
}
#include "frontend_flow.inc"
#include "frontend_career_info.inc"
bool FrontendArt::load_post_race(const std::filesystem::path& root,std::string& error) {
    try {
        if(impl_->textures.empty())throw std::runtime_error("post-race needs resident frontend fonts");
        std::vector<unsigned char> image;std::vector<RaceTableIssue> issues;
        if(!load_race_image(root/"Global/INGAMEB.BUN",image,issues) ||
           sha256_hex(sha256(image.data(),image.size()))!="044724AB4C167831587E80F29961A1E7D8404C9DD5266533F3563C555A997299")
            throw std::runtime_error("post-race input identity mismatch");
        std::vector<unsigned char> language;
        if(!load_race_image(root/"Languages/English.bin",language,issues) ||
           sha256_hex(sha256(language.data(),language.size()))!="6CBAB64235523149B95966B1844CDCA9FB858744126F8D9AD10E72C921AB8790")
            throw std::runtime_error("post-race language identity mismatch");
        const auto label=[&](unsigned hash,const std::string& fallback) {
            const auto count=dword(language.data()+12),table=8+dword(language.data()+16),strings=8+dword(language.data()+20);
            if(table>language.size() || count>(language.size()-table)/8 || strings>language.size())
                throw std::runtime_error("post-race language bounds");
            for(unsigned i=0;i<count;++i)if(dword(language.data()+table+i*8)==hash) {
                const auto offset=dword(language.data()+table+i*8+4);
                if(offset>=language.size()-strings)throw std::runtime_error("post-race language offset");
                return ascii(language.data()+strings+offset,language.size()-strings-offset);
            }
            return fallback;
        };
        std::array<std::vector<Object>,5> pages;
        std::array<std::map<unsigned,std::vector<unsigned char>>,5> scripts;
        const std::size_t offsets[]={0x164f0c,0x16368c,0x161e0c,0x16708c,0x16670c};
        for(unsigned page=0;page<5;++page) {
            const auto at=offsets[page];std::vector<unsigned char> fng;std::size_t used=0;
            if(!jdlz_decompress(image.data()+at,dword(image.data()+at+12),1024*1024,fng,used,error))
                throw std::runtime_error("post-race FNG decode failed");
            for(std::size_t pos=0;pos+16<=fng.size();++pos) {
                if(std::memcmp(fng.data()+pos,"FOb\xEA",4))continue;
                const auto n=dword(fng.data()+pos+12),outer=dword(fng.data()+pos+4);
                if(n>fng.size()-pos-16 || outer<n+8 || outer>fng.size()-pos-8)throw std::runtime_error("post-race object bounds");
                std::vector<unsigned char> bytes(fng.begin()+pos,fng.begin()+pos+16+n);
                const unsigned clipped=n+8;std::memcpy(bytes.data()+4,&clipped,4);
                auto o=decode_ui_object(bytes,0);
                const auto* script=fng.data()+pos+16+n;const auto length=outer-n-8;
                scripts[page][o.id]=std::vector<unsigned char>(script,script+length);
                post_state(o,script,length,"Init");
                if(page>=3 && o.name=="RewardBlurb")post_state(o,script,length,"AnimateIn");
                if(o.type==2 && o.language!=0xffffffffU)o.text=label(o.language,o.text);
                if(page==0 && (o.name=="Button1" || o.name=="Button2" || o.name=="Button3" ||
                   o.name=="Button4" || o.name=="Button5" || o.name=="Button6" ||
                   o.name=="Button_1" || o.name=="Button_2" || o.name=="Button_3" ||
                   o.name=="Button_4" || o.name=="Button_5" || o.name=="Button_6")) {
                    auto high=o;post_state(o,script,length,"Unhighlight");post_state(high,script,length,"Highlight");
                    // Save original steady states; selection indexes two copies.
                    high.name+="/selected";pages[page].push_back(std::move(high));
                }
                pages[page].push_back(std::move(o));pos+=7+outer;
            }
        }
        if(pages[0].size()<12 || pages[1].size()<80 || pages[2].size()<80 || pages[3].size()<40 || pages[4].size()<16)throw std::runtime_error("post-race objects absent");
        std::vector<unsigned char> global;
        if(!load_race_image(root/"Global/GLOBALB.BUN",global,issues))throw std::runtime_error("style threshold input unreadable");
        std::vector<unsigned> thresholds;
        for(std::size_t at=0;at+8<=global.size();) {
            const auto kind=dword(global.data()+at),size=dword(global.data()+at+4);
            if(size>global.size()-at-8)throw std::runtime_error("style threshold chunk bounds");
            if(kind==0x34a08) {
                const auto first=(at+23)&~std::size_t(15);const auto count=size/32;
                if(first+count*32>at+8+size)throw std::runtime_error("style threshold records");
                for(std::size_t i=0;i<count;++i)thresholds.push_back(dword(global.data()+first+i*32));
            }
            at+=8+size;
        }
        if(thresholds.empty())throw std::runtime_error("style reward thresholds missing");
        impl_->style_thresholds=std::move(thresholds);
        impl_->dialogs[0]=read_dialog(image,0x175e0c,{0xe2a5c626U,impl_->large_hash,impl_->small_hash},label,error);
        impl_->dialogs[1]=read_dialog(image,0x173b0c,{0xe2a5c626U,0x1abedeb1U,bin_hash("NFSU_LOGO"),impl_->large_hash},label,error);
        impl_->dialogs[3]=read_dialog(image,0x16828c,{impl_->large_hash,0xe2a5c626U,0x1abedeb1U,bin_hash("NFSU_LOGO")},label,error);
        impl_->post_objects=std::move(pages);impl_->post_scripts=std::move(scripts);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool FrontendArt::prepare_post_race(FrontendArt& target,std::string& error) const {
    try {
        if(this==&target)throw std::runtime_error("post-race target must be independent");
        for(const auto& page:impl_->post_objects)
            if(page.empty())throw std::runtime_error("post-race objects not loaded");
        auto next=std::make_unique<Impl>();
        next->large=impl_->large;next->small=impl_->small;
        next->large_hash=impl_->large_hash;next->small_hash=impl_->small_hash;
        for(unsigned i=0;i<4;++i)next->dialogs[i]=impl_->dialogs[i];
        next->post_objects=impl_->post_objects;next->post_scripts=impl_->post_scripts;next->style_thresholds=impl_->style_thresholds;
        // draw_post_race uses these two fonts, White16x16 and45Corner only.
        for(unsigned hash:{next->large_hash,next->small_hash,0xe2a5c626U,0x1abedeb1U,bin_hash("NFSU_LOGO")}) {
            const auto texture=impl_->textures.find(hash);
            if(texture==impl_->textures.end())throw std::runtime_error("post-race texture not loaded");
            next->textures.emplace(hash,texture->second);
        }
        next->pack_atlas(true);
        target.impl_=std::move(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void FrontendArt::Impl::draw_post_race(Canvas image,const PostRaceMenu& menu,const PostRaceValues& values)const {
    const unsigned page=static_cast<unsigned>(menu.page());
    if(menu.confirming()) {
        auto labels=values;
        const auto action=menu.confirmation();
        labels["message_blurb"]=action==PostRaceAction::restart?"Restart Race?":
            action==PostRaceAction::restart_tournament?"Start Tournament again from^the beginning?":
            action==PostRaceAction::next_race?"Advance to next race?":
            action==PostRaceAction::quit_tournament?"Quit Tournament? ^Quitting a tournament causes you to lose ^your tournament progress. Continue?":
            values.count("CollectReward")?"Collect Reward?":"Continue?";
        labels["button1"]="OK";labels["button2"]="Cancel";labels["Controls"]="A: Accept   B: Cancel";
        const unsigned key=100+menu.revision();
        if(dialog_key!=key){dialog_key=key;dialog_started=ui_ticks;}
        draw_dialog(image,3,labels,menu.confirmed()?0:1,temporal?unsigned(std::min<std::uint64_t>(ui_ticks-dialog_started,0xffffff)):~0U,
                    menu.fading()?menu.fade_tick():~0U);
        return;
    }
    auto sampled=post_objects.at(page);
    if(temporal) {
        const std::uint64_t key=page+8*menu.reward_index()+(std::uint64_t(menu.revision())<<32);
        if(key!=post_page_key){post_page_key=key;post_started=ui_ticks;post_selected=post_old_selected=~0U;}
        const unsigned tick=unsigned(std::min<std::uint64_t>(ui_ticks-post_started,0xffffff));
        if(!page&&post_selected!=menu.selected()) {
            post_old_selected=post_selected;post_selected=menu.selected();post_selected_started=ui_ticks;
        }
        const auto choice_tick=unsigned(std::min<std::uint64_t>(ui_ticks-post_selected_started,0xffffff));
        for(auto& o:sampled) {
            const auto found=post_scripts[page].find(o.id);if(found==post_scripts[page].end())continue;
            const auto& script=found->second;
            post_state(o,script.data(),script.size(),"Init",tick);
            if(!page) {
                if(o.name=="TopBacking"&&menu.count()>3)post_state(o,script.data(),script.size(),menu.count()==4?"Init04":"Init05",tick);
                for(unsigned n=1;n<=6;++n) {
                    const auto button="Button"+std::to_string(n),back="Button_"+std::to_string(n);
                    if(o.name==button||o.name==back||o.name==button+"/selected"||o.name==back+"/selected") {
                        const bool high=n-1==menu.selected();
                        post_state(o,script.data(),script.size(),high?"Highlight":"Unhighlight",high||post_old_selected==n-1?choice_tick:~0U);
                    }
                }
            }
            if(menu.fading())post_state(o,script.data(),script.size(),"FadeOut",menu.fade_tick());
            if(page>=3&&o.name=="RewardBlurb")post_state(o,script.data(),script.size(),"AnimateIn",tick);
            if(page==3) {
                const char* clock=o.name=="StyleCategory"?"CategoryFlashTick":o.name=="CountingStylePoints"?"CountingFlashTick":
                    o.name=="StyleMultiplier"||o.name=="StyleMultiplierHeading"?"MultiplierFlashTick":o.name=="TotalPointsAnimate"?"TotalFlashTick":nullptr;
                if(clock){const auto it=values.find(clock);if(it!=values.end())post_state(o,script.data(),script.size(),"Flashy",unsigned(std::stoul(it->second)));}
            }
        }
    }
    const auto& objects=sampled;
    const auto white=textures.find(0xe2a5c626U),corner=textures.find(0x1abedeb1U);
    if(objects.empty() || white==textures.end() || corner==textures.end())return;
    const auto transform=[&](auto&& self,const Object& o,unsigned depth)->Transform {
        if(depth>32)throw std::runtime_error("post-race parent cycle");
        Transform p;
        if(o.parent) {
            auto it=std::find_if(objects.begin(),objects.end(),[&](const Object& v){return v.id==o.parent;});
            if(it==objects.end())throw std::runtime_error("post-race parent absent");p=self(self,*it,depth+1);
        }
        return combine(p,o,o.type==2 || o.type==5);
    };
    std::vector<std::size_t> order(objects.size());for(std::size_t i=0;i<order.size();++i)order[i]=i;
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return transform(transform,objects[a],0).z>transform(transform,objects[b],0).z;});
    for(auto i:order) {
        const auto& o=objects[i];if(o.type==5)continue;
        if(page==3) {
            const auto phase=values.find("StyleStage");const unsigned stage=phase==values.end()?2:unsigned(std::stoul(phase->second));
            const auto hidden=[&](auto&& self,const Object& v,unsigned depth)->bool {
                if(depth>32)throw std::runtime_error("style group cycle");
                if((v.name=="StyleMultiplierGroup"&&stage==0)||(v.name=="TotalPointsGroup"&&stage<2))return true;
                if(v.parent)for(const auto& parent:objects)if(parent.id==v.parent)return self(self,parent,depth+1);
                return false;
            };
            if(hidden(hidden,o,0))continue;
        }
        std::string value=o.text;
        if(!page) {
            unsigned index=6;
            for(unsigned n=1;n<=6;++n) {
                const auto button="Button"+std::to_string(n),back="Button_"+std::to_string(n);
                if(o.name==button || o.name==back || o.name==button+"/selected" || o.name==back+"/selected")index=n-1;
            }
            if(index<6) {
                if(index>=menu.count())continue;
                const bool high=o.name.find("/selected")!=std::string::npos;
                if(high!=(index==menu.selected()))continue;
                const char* ordinary[]={"Continue","Statistics","Restart"};
                const char* tournament[]={menu.next_race()?"Quit Tournament":"Continue","Statistics","Restart Tournament","Restart Race","Next Race"};
                if(o.type==2)value=menu.tournament()?tournament[index]:index==0&&values.count("CollectReward")?"Collect Reward":ordinary[index];
            }else if(o.name!="TopBacking"&&o.name!="White16x16(14)"&&o.name!="45Corner")continue;
        }else {
            if(o.name=="ConduitMdITC_TT21i" || o.name=="quit_tag" || o.name=="next_tag" ||
               o.name=="DisplayMenuBar" || o.name=="mouse_over_L" || o.name=="mouse_over_R" ||
               o.name=="mouse_over_select" || o.name=="OptionsIcon" || o.name=="TRIANGLE BUTTON" ||
               o.name=="back" || o.name=="ONLINE_REP_Backing02" || o.name.rfind("Rep_",0)==0)continue;
            if(page==2 && (o.name.rfind("RacerColor_",0)==0 || o.name.rfind("CarColor_",0)==0 ||
               (!values.count("Tournament")&&(o.name.rfind("PointEarned_",0)==0 || o.name.rfind("TotalPoints_",0)==0)) ||
               o.name=="Online_2P_Backing"))continue;
            if(page==1) {
                auto mode=values.find("RaceMode");
                if(mode!=values.end() && (mode->second=="drag" || mode->second=="sprint") &&
                   (o.name=="TextBack_1" || o.name=="TextBack_2" || o.name=="TextBack_3" ||
                    o.name=="TextBack_4" || o.name=="TextBack_5"))continue;
            }
            if(o.type==2) {
                auto found=values.find(o.name);if(found!=values.end())value=found->second;
                else if(o.name.rfind("DataName_",0)==0 || o.name.rfind("Data_",0)==0 ||
                        o.name.rfind("AllLapTimes_",0)==0 || o.name.rfind("PositionAfterEachLap_",0)==0 ||
                        o.name.rfind("LapNumber_",0)==0 || page==2)value="";
                if(page==2 && o.name=="font_ArialBlack16(29)")value="Next";
                if(page==3 && o.name=="font_ArialBlack19")value="Next";
            }
        }
        const auto m=transform(transform,o,0);
        auto color=o.color;
        const Object* child=&o;
        for(unsigned depth=0;child->parent && depth<32;++depth) {
            const auto parent=std::find_if(objects.begin(),objects.end(),[&](const Object& v){return v.id==child->parent;});
            if(parent==objects.end())throw std::runtime_error("post-race color parent absent");
            for(unsigned k=0;k<4;++k)color[k]=(color[k]*parent->color[k]+127)/255;
            child=&*parent;
        }
        constexpr unsigned white_resource[]={1,1,0,0,0},corner_resource[]={3,2,4,3,2},small_resource[]={2,3,2,2,4};
        if(o.type==2)text(image,o,m,value,color,o.resource==small_resource[page]?8:0);
        else if(o.resource==white_resource[page] || o.resource==corner_resource[page] ||
                (page==3&&o.resource==4) || (page==4&&o.resource==3)) {
            const auto& tex=o.resource==corner_resource[page]?corner->second:white->second;
            float width=o.w;
            if(page==3 && o.name=="RewardStyleBar") {
                const auto it=values.find("CareerStyleDisplayed");
                const auto points=it==values.end()?0:std::stoll(it->second);
                std::int64_t lower=0,upper=0;
                for(unsigned threshold:style_thresholds) {
                    if(threshold<points)lower=std::max(lower,std::int64_t(threshold));
                    if(threshold>points && (!upper||threshold<upper))upper=threshold;
                }
                const float fraction=upper?std::clamp(float(points-lower)/float(upper-lower),0.F,1.F):1.F;
                width*=fraction;
            }
            quad(image,tex,m,-o.w/2,-o.h/2,width,o.h,o.uv,color,o.type==9?&o.corner_colors:nullptr);
        }
    }
    Object hint;hint.justification=0;Transform footer;footer.x=-280;footer.y=218;
    text(image,hint,footer,menu.statistics()?"Left / Right: Racer   A / B: Back":menu.page()==PostRacePage::menu?"A: Accept   B: Leave":menu.page()==PostRacePage::results?"A: Next":"A: Next   B: Previous",{{242,233,207,255}},8);

}
void FrontendArt::draw_post_race(ScreenImage image,const PostRaceMenu& menu,const PostRaceValues& values)const {
    impl_->draw_post_race(Canvas{image},menu,values);
}
void FrontendArt::record_post_race(unsigned width,unsigned height,const PostRaceMenu& menu,
                                  const PostRaceValues& values,FrontendUiFrame& frame)const {
    frame.vertices.clear();frame.additive.clear();frame.background_quads=0;
    impl_->draw_post_race(Canvas{width,height,impl_->atlas,frame},menu,values);
}
#include "career_ui.inc"
void FrontendArt::draw_free_drive(ScreenImage image,const CareerCatalog& catalog,const CareerProfile& profile,
    unsigned selection,unsigned course,const std::string& notice,const QuickRaceOptions* options,bool choose_mode)const {
    impl_->draw_free_drive(Canvas{image},catalog,profile,selection,course,notice,options,choose_mode);
}
void FrontendArt::record_free_drive(unsigned width,unsigned height,const CareerCatalog& catalog,
    const CareerProfile& profile,unsigned selection,unsigned course,const std::string& notice,FrontendUiFrame& frame,
    const QuickRaceOptions* options,bool choose_mode)const {
    frame.vertices.clear();frame.additive.clear();frame.background_quads=0;
    impl_->draw_free_drive(Canvas{width,height,impl_->atlas,frame},catalog,profile,selection,course,notice,options,choose_mode);
}
void FrontendArt::Impl::draw_free_drive(Canvas canvas,const CareerCatalog& catalog,const CareerProfile& profile,
    unsigned selection,unsigned course,const std::string& notice,const QuickRaceOptions* options,bool choose_mode)const {
    const auto& track=catalog.free_courses().at(course);
    const auto label=[&](float x,float y,float scale,const std::string& text,std::array<unsigned,4> color) {
        Object o;o.justification=0;Transform m;m.x=x;m.y=y;m.a=m.d=scale;this->text(canvas,o,m,text,color,8);
    };
    const auto white=textures.find(0xe2a5c626U);
    if(white!=textures.end()){Transform m;quad(canvas,white->second,m,-303,-220,605,306,{{0,0,1,1}},{{0,0,0,185}});}
    const QuickRaceOptions defaults;
    const auto& config=options?*options:defaults;
    constexpr std::array<unsigned,4> bright{{242,233,207,255}},dim{{180,154,84,255}};
    label(-280,-198,1.0F,choose_mode?"QUICK RACE":quick_race_mode_name(config.mode),bright);
    const auto row=[&](unsigned index,const std::string& value) {
        label(-280,-158+float(index)*(choose_mode?33.F:21.F),.72F,
            (selection==index?"> ":"  ")+value,selection==index?bright:dim);
    };
    if(choose_mode) {
        for(unsigned i=0;i<kQuickRaceModeCount;++i)row(i,quick_race_mode_name(static_cast<QuickRaceMode>(i)));
        label(-280,192,.65F,"UP/DOWN: Select   A: Enter   B: Back",bright);
        return;
    }
    const unsigned level=profile.ride.performance[2];const char* levels[]={"STOCK","STREET","PRO","EXTREME"};
    const bool unique=std::all_of(profile.ride.unique.begin(),profile.ride.unique.end(),[](bool value){return value;});
    constexpr const char* traffic[]={"NONE","MINIMUM","MODERATE","MAXIMUM"};
    constexpr const char* difficulty[]={"EASY","MEDIUM","HARD"};
    unsigned index=0;
    for(auto field:quick_race_rows(config.mode,track.type,!profile.bonus_preset.empty())) {
        using Row=QuickRaceRow;
        std::string value;
        switch(field) {
        case Row::track:value="TRACK: "+track.name+(track.direction?" (REVERSE)":"");break;
        case Row::car:value="CAR: "+catalog.quick_car_label(profile);break;
        case Row::customize:value="CUSTOMIZE RIDE";break;
        case Row::performance:value="PERFORMANCE: "+
            (std::all_of(profile.ride.performance.begin(),profile.ride.performance.end(),[&](unsigned v){return v==level;})?
                std::string(levels[std::min(3U,level)]):std::string("CUSTOM"));break;
        case Row::unique:value=std::string("UNIQUE PERFORMANCE: ")+(unique?"ON":"OFF / CUSTOM");break;
        case Row::traffic:value=std::string("TRAFFIC: ")+traffic[config.traffic];break;
        case Row::difficulty:value=std::string("DIFFICULTY: ")+difficulty[config.difficulty];break;
        case Row::opponents:value="OPPONENTS: "+std::to_string(config.opponents);break;
        case Row::laps:value="LAPS: "+std::to_string(config.laps);break;
        case Row::catch_up:value=std::string("CATCH UP: ")+(config.catch_up?"ON":"OFF");break;
        case Row::start:value=config.mode==QuickRaceMode::free_run?"START DRIVING":"START RACE";break;
        }
        row(index++,value);
    }
    label(-280,192,.65F,"UP/DOWN: Select   LEFT/RIGHT: Change   A: Enter   B: Back   Y: Stock",bright);
    if(!notice.empty())label(-280,219,.6F,notice.substr(0,100),{{255,210,120,255}});
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
        for(const auto& object:impl_->objects)if(object.type==2 && !object.chyron)
            impl_->start_named_state(object,"Init",0);
    }
    impl_->temporal=true;impl_->ui_ticks+=static_cast<unsigned>(double(seconds)*1200);
    impl_->chyron_ticks+=double(seconds)*4000;
}
const FrontendUiAtlas& FrontendArt::atlas() const {return impl_->atlas;}
void FrontendArt::record_ui(unsigned width,unsigned height,const FrontendMenu& menu,FrontendUiFrame& frame) const {
    frame.vertices.clear();frame.additive.clear();frame.background_quads=0;
    frame.vertices.reserve(kFrontendUiMaxQuads*4);frame.additive.reserve(kFrontendUiMaxQuads);
    impl_->draw(Canvas{width,height,impl_->atlas,frame},menu);
}
void FrontendArt::Impl::draw(Canvas image,const FrontendMenu& menu) const {
    const auto& p=*this;
    p.select(menu.selected());
    const auto song_tick=static_cast<std::uint32_t>(p.chyron_ticks);
    if(p.song_revision!=menu.song_revision()) {
        p.chyron.show(song_tick);p.song_revision=menu.song_revision();
        for(const auto& object:p.objects)if(object.chyron) {
            p.start_named_state(object,"Init",p.ui_ticks);
            p.start_named_state(object,"Show",p.ui_ticks);
        }
    }
    if(p.chyron.update(song_tick)==FrontendChyron::Action::hide)
        for(const auto& object:p.objects)if(object.chyron)p.start_named_state(object,"Hide",p.ui_ticks);

    ++p.sample_generation;
    std::vector<std::size_t> order(p.objects.size());for(std::size_t i=0;i<order.size();++i) order[i]=i;
    std::stable_sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b) {return p.transform(p.objects[a]).z>p.transform(p.objects[b]).z;});
    const unsigned hashes[]={0xe2a5c626,0x1f3bdb88,0x1abedeb1,0,bin_hash("ARROW3"),bin_hash("CIRCLE"),0x868a1fcf,bin_hash("NFSU_LOGO")};
    for(auto i:order) {
        const auto& o=p.sample(p.objects[i]);if((o.chyron && !p.chyron.visible()) || !p.visible(o,menu.selected()) || o.type==5 || o.name=="NFSU_logo") continue;
        auto color=o.color;
        // Original steady states from UnHighlight/Highlight, after Init fades.
        if(!p.temporal && !o.chyron && !color[3] && (o.type==2 || o.type==5)) color[3]=255;
        if(!color[3]) continue;
        const auto m=p.transform(p.objects[i]);
        if(!o.chyron && (o.type==1 || o.type==9) && m.y>150) continue;
        if(o.type==2) {
            if(o.chyron?o.resource!=0:(o.resource!=3 && o.resource!=8)) continue;
            std::string label=o.text;
            // Genuine 4D76A0 -> 4D7580 targets F5387816 with artist and
            // 11DB9E17 with the quoted title; authored names are reversed.
            if(o.chyron && o.name=="TrackTitle")label=menu.artist();
            else if(o.chyron && o.name=="ArtistName") {
                label=menu.title();
                if(label.size()<2 || label.front()!='"' || label.back()!='"')
                    label="\""+label+"\"";
            }
            else if(o.chyron && o.name=="AlbumTitle")label=menu.album();
            const bool underground=o.name=="UndergroundMode",free=o.name=="QuickRace";
            if(free) label="QUICK RACE";
            if(o.name=="Customize") label="CUSTOMIZE RIDE";
            const bool online=o.name=="Online",lan=online&&menu.multiplayer_enabled();
            const bool options=o.name=="Options"&&menu.options_enabled();
            const bool lan_selected=lan&&menu.selected()==menu.multiplayer_selection();
            if(online) label=lan_selected?"> MULTIPLAYER":"MULTIPLAYER";
            if(options&&menu.selected()==2) label="> OPTIONS";
            if(o.name=="Description") label=menu.selected()==0?"Begin your Underground career":
                menu.selected()==1?"Set up a race or explore a track in Free Run.":
                menu.options_enabled()&&menu.selected()==2?"Change display options.":"Host or join a race over your local network.";
            if((underground || free) && !p.temporal) color=(menu.selected()==(free?1U:0U))?std::array<unsigned,4>{{242,233,207,255}}:std::array<unsigned,4>{{180,154,84,255}};
            else if(lan||options)color=(lan_selected||(options&&menu.selected()==2))?std::array<unsigned,4>{{242,233,207,color[3]}}:std::array<unsigned,4>{{180,154,84,color[3]}};
            else if(o.name=="STATISTICS" || o.name=="Options" || o.name=="Customize" || online)
                color={{110,110,110,p.temporal?(color[3]*180+127)/255:180}};
            color=p.parent_color(o,color);
            p.text(image,o,m,label,color,o.chyron?3:o.resource);
            if(online&&!lan) {
                // Keep the prototype parked: this authored row is disabled.
                Object badge;badge.justification=o.justification;
                auto position=m;position.y+=18;
                p.text(image,badge,position,"COMING SOON",color,8);
            }
        } else if((o.type==1 || o.type==9) && o.resource<(o.chyron?4U:8U)) {
            const unsigned chyron_hashes[]={0,0x5ed3f261,0xe2a5c626,0x1abedeb1};
            const auto tex=p.textures.find(o.chyron?chyron_hashes[o.resource]:hashes[o.resource]);if(tex==p.textures.end()) continue;
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
    Object help;help.justification=0;
    Transform footer;footer.x=-266;footer.y=219;
    p.text(image,help,footer,"A: Enter   X: Profiles   B: Exit",{{242,233,207,255}},8);
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
