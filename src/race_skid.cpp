#include "nfsu/race_skid.h"
#include "nfsu/texture_decode.h"
#include "nfsu/texture.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace nfsu {
namespace {
double dot(VehicleVec3 a,VehicleVec3 b){return (a.x*b.x+a.y*b.y)+a.z*b.z;}
VehicleVec3 sub(VehicleVec3 a,VehicleVec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
double norm(VehicleVec3 v){return std::sqrt(dot(v,v));}
}
bool original_wheel_skid(const WheelSkidInput& in,WheelSkidSample& out) noexcept {
    out={};
    if(!in.contact||in.inhibited||in.surface>=29||!std::isfinite(in.width)||in.width<=0||
       !std::isfinite(in.friction_usage)||!std::isfinite(in.load_kn)||
       !(in.sliding||double(in.friction_usage)*in.load_kn>8.0))return false;
    for(double x:{in.velocity.x,in.velocity.y,in.side.x,in.side.y,in.side.z,
                  in.point.x,in.point.y,in.point.z,in.normal.x,in.normal.y,in.normal.z})
        if(!std::isfinite(x))return false;
    const float x=float(in.velocity.x),y=float(in.velocity.y);
    const double speed=std::sqrt(double(x)*x+double(y)*y);if(!(speed>1))return false;
    const float px=float(-y/speed),py=float(x/speed);
    const float facing=float(std::abs(double(float(in.side.y))*py+double(float(in.side.x))*px));
    float width=in.width*((in.surface==8||in.surface==9||in.surface==15)?1.399999976158142F:1.F);
    width=float(double(width)*std::max(double(facing),.5)*.5);
    out.cross_section={float(double(px)*width),float(double(py)*width),0};
    if(!(std::abs(in.normal.z)>.1))return false;
    out.cross_section.z=-(in.normal.x*out.cross_section.x+in.normal.y*out.cross_section.y)/in.normal.z;
    out.point=in.point;
    out.opacity=in.surface==13?0.F:std::max(.30000001192092896F,
        in.sliding?facing:float(double(facing)*.3333333432674408F));
    if(in.surface==0||in.surface==14||in.surface>=20)return false;
    out.texture=(in.surface==8||in.surface==9)?1:in.surface==15?2:0;
    return out.opacity>0;
}
bool RaceSkidMarks::load(const std::filesystem::path& root,const CarAssets& car,std::string& error) {
    reset();textures_={};
    std::ifstream f(root/"Global/GLOBALB.BUN",std::ios::binary);
    for(unsigned i=0;i<4;++i) {
        unsigned char b[4]{};f.seekg(static_cast<std::streamoff>(car.record_offset+kVehicleWheelOffset+i*kVehicleWheelStride+0x14));
        if(!f.read(reinterpret_cast<char*>(b),4)){error="skid wheel width unavailable";return false;}
        const std::uint32_t bits=b[0]|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);
        std::memcpy(&widths_[i],&bits,4);
        if(!std::isfinite(widths_[i])||widths_[i]<=0||widths_[i]>2){error="invalid skid wheel width";return false;}
    }
    const char* names[]={"SKID_ROAD","SKID_DIRT","SKID_GRASS"};
    std::vector<std::uint32_t> wanted;for(auto name:names)wanted.push_back(bin_hash(name));std::sort(wanted.begin(),wanted.end());
    for(const auto* file:{"Global/INGAMEB.BUN","Global/GLOBALB.BUN"}) {
        std::vector<unsigned char> bytes;const auto packs=load_texture_pack_set(root/file,bytes);
        RenderTextureSource source;source.bytes=&bytes;source.complete=packs.complete;source.wanted=&wanted;
        for(const auto& pack:packs.packs)source.packs.push_back(&pack);
        RenderTextureSet decoded;RenderTextureLimits limits;limits.retain_mips=true;
        if(!decode_render_texture_sources({source},limits,decoded,error))return false;
        for(unsigned i=0;i<3;++i)if(!textures_[i].usable())
            for(auto& texture:decoded.textures)if(texture.claims_hash(bin_hash(names[i]))&&texture.usable())textures_[i]=std::move(texture);
    }
    for(const auto& t:textures_)if(!t.usable()){error="original skid texture unavailable";return false;}
    for(unsigned i=0;i<kSegments;++i){const unsigned short b=static_cast<unsigned short>(i*4);
        const unsigned short six[]={b,static_cast<unsigned short>(b+1),static_cast<unsigned short>(b+2),b,static_cast<unsigned short>(b+2),static_cast<unsigned short>(b+3)};
        std::copy_n(six,6,indices_.begin()+i*6);}
    error.clear();return true;
}
void RaceSkidMarks::reset() noexcept {links_={};segments_={};visible_={};next_=count_=0;tick_=0;have_tick_=false;dirty_=true;}
void RaceSkidMarks::observe(unsigned wheel,const WheelSkidSample* sample) noexcept {
    if(wheel>=links_.size())return;
    auto& link=links_[wheel];
    if(!sample){link={};return;}
    if(!link.active||link.last.texture!=sample->texture||norm(sub(sample->point,link.last.point))>4) {
        link={};link.active=true;link.last=*sample;return;
    }
    const auto delta=sub(sample->point,link.last.point);const double length=norm(delta);
    if(length<.002)return;
    if(link.has_slot) {
        auto& old=segments_[link.slot];const auto old_delta=sub(old.b.point,old.a.point);
        const auto total=sub(sample->point,old.a.point);const double old_length=norm(old_delta),total_length=norm(total);
        const double angle=old_length>0?1-dot(old_delta,delta)/(old_length*length):0;
        //5894E0 coalesces short/straight input, bounded to3m per segment.
        if(total_length<=3 && (total_length<.25||angle<.002)){old.b=*sample;link.last=*sample;dirty_=true;return;}
    }
    for(auto& other:links_)if(other.has_slot&&other.slot==next_)other.has_slot=false;
    segments_[next_]={link.last,*sample};dirty_=true;link.slot=next_;link.has_slot=true;
    next_=(next_+1)%kSegments;count_=std::min<std::size_t>(count_+1,kSegments);link.last=*sample;
}
void RaceSkidMarks::advance(const RaceSession& session) noexcept {
    const auto tick=session.timing().step;
    if(have_tick_&&tick==tick_)return;
    if(have_tick_&&tick<tick_)reset();
    for(const auto& step:session.physics_telemetry()) {
        if(step.reset!=VehicleTrackReset::none) {
            for(unsigned w=0;w<4;++w)observe(w,nullptr);
            continue;
        }
        for(unsigned w=0;w<4;++w) {
            const auto& wheel=step.model.wheels[w];WheelSkidInput in;
            in.point=wheel.ground_point;in.normal=wheel.ground_normal;
            in.velocity=wheel.skid_velocity;in.side=wheel.skid_side;
            in.width=widths_[w];in.friction_usage=wheel.skid_friction_usage;in.load_kn=wheel.skid_load_kn;
            in.surface=wheel.surface;in.contact=wheel.contact;in.sliding=wheel.sliding;
            WheelSkidSample sample;observe(w,original_wheel_skid(in,sample)?&sample:nullptr);
        }
    }
    tick_=tick;have_tick_=true;rebuild();
}
void RaceSkidMarks::rebuild() noexcept {
    if(!dirty_)return;
    dirty_=false;
    visible_={};
    for(std::size_t i=0;i<count_;++i) {
        const auto& s=segments_[(next_+kSegments-count_+i)%kSegments];if(s.b.texture>2)continue;
        const unsigned t=s.b.texture;const auto offset=t*kSegments*4+visible_[t]++*4;
        for(unsigned c=0;c<4;++c) {
            auto& v=vertices_[offset+c];v={};const auto& p=c<2?s.a:s.b;const double sign=(c==0||c==3)?-1:1;
            v.px=float(p.point.x+sign*p.cross_section.x);v.pz=float(p.point.y+sign*p.cross_section.y);
            v.py=float(p.point.z+sign*p.cross_section.z+.05);
            v.u=(c==0||c==3)?0:1;v.v=c<2?0:1;
            //40BE70 default RGB128;589810 halves opacity before world.efx RGBA*2.
            v.color[0]=v.color[1]=v.color[2]=128;
            v.color[3]=static_cast<unsigned char>(std::clamp(int(p.opacity*255.F),0,255)>>1);
        }
    }
    for(unsigned t=0;t<3;++t)for(std::size_t i=visible_[t]*4;i<kSegments*4;++i)vertices_[t*kSegments*4+i]={};
}
bool RaceSkidMarks::append(RecordedCommands& commands,std::string& error) {
    rebuild();
    auto& table=commands.owning_uploads();const auto first=table.resources.size();
    for(unsigned t=0;t<3;++t) {
        UploadResource v;v.kind=UploadResourceKind::vertex_buffer;v.name="skid/vertices/"+std::to_string(t);
        v.data=reinterpret_cast<const unsigned char*>(vertices_.data()+t*kSegments*4);v.streamed_vertices=true;
        v.bytes=kSegments*4*sizeof(MeshVertex);v.element_bytes=sizeof(MeshVertex);v.elements=kSegments*4;v.alignment=kCarVertexAlignment;
        table.resources.push_back(v);
        UploadResource index;index.kind=UploadResourceKind::index_buffer;index.name="skid/indices/"+std::to_string(t);
        index.data=reinterpret_cast<const unsigned char*>(indices_.data());index.bytes=sizeof(indices_);index.element_bytes=2;index.elements=indices_.size();index.alignment=kIndexAlignment;
        table.resources.push_back(index);
        const auto& texture=textures_[t];UploadResource image;image.kind=UploadResourceKind::texture;image.memory=UploadMemory::image;
        image.name="skid/texture/"+std::to_string(t);image.data=texture.rgba.data();image.bytes=texture.rgba.size();image.alignment=kTextureStagingAlignment;
        image.element_bytes=4;image.elements=image.bytes/4;image.texture_hash=texture.hash;
        if(!check_texture_upload_format(TextureUploadFormat::rgba8,texture.width,texture.height,image.bytes,image.texture,error,texture.mip_count))return false;
        table.resources.push_back(image);
        auto& totals=table.totals;totals.resources+=3;++totals.vertex_buffers;++totals.index_buffers;++totals.textures;
        totals.vertex_bytes+=v.bytes;totals.index_bytes+=index.bytes;totals.texture_bytes+=image.bytes;
        totals.largest_texture_bytes=std::max(totals.largest_texture_bytes,image.bytes);
        RecordedDraw d;d.name="skid/material/"+std::to_string(t);d.group=0x187000+t;
        d.vertex_resource=first+t*3;d.index_resource=d.vertex_resource+1;d.texture_resource=d.vertex_resource+2;
        d.vertex_count=kSegments*4;d.index_count=kSegments*6;
        for(const auto& p:shader_manifest::kPrograms)if(std::strcmp(p.name,"car_textured")==0)d.program=&p;
        if(!d.program){error="skid program absent";return false;}
        bool found=false;for(std::size_t i=0;i<d.program->variant_count;++i)
            if(std::strcmp(d.program->variants[i].name,"source_over")==0){d.pipeline=d.program->variants[i];found=true;}
        if(!found){error="skid blend absent";return false;}
        d.pipeline.cull=shader_manifest::Cull::none;d.pipeline.depth_write=false;
        d.material.fallback[3]=9;d.material.alpha[0]=0;
        commands.owning_draws().push_back(d);
    }
    return true;
}
void RaceSkidMarks::place(RecordedCommands& commands,std::size_t first) {
    rebuild();
    for(unsigned t=0;t<3;++t)commands.owning_draws().at(first+t).material.alpha[0]=visible_[t]?1.F:0.F;
}
std::size_t RaceSkidMarks::resident_bytes() const noexcept {
    std::size_t n=sizeof(*this);for(const auto& t:textures_)n+=t.rgba.capacity();return n;
}
}
