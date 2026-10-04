#include "nfsu/minimap.h"
#include "nfsu/race_events.h"
#include "nfsu/sha256.h"
#include "nfsu/texture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

namespace nfsu {
namespace {
bool finite(MapPoint p) noexcept { return std::isfinite(p[0]) && std::isfinite(p[1]); }
bool finite_polygon(const std::array<MapPoint,4>& points) noexcept {
    return std::all_of(points.begin(),points.end(),[](MapPoint p) { return finite(p); });
}
bool finite_vertex(const MinimapVertex& v) noexcept {
    return finite(v.screen) && std::all_of(v.uvq.begin(),v.uvq.end(),
        [](float x) { return std::isfinite(x); });
}
float f32(const unsigned char* p) { float f; std::memcpy(&f,p,4); return f; }
std::uint16_t u16(const unsigned char* p) { return std::uint16_t(p[0] | (unsigned(p[1])<<8)); }
MapPoint rotate(MapPoint p, std::uint16_t a) noexcept {
    const double s=minimap_scalar_sine(a), c=minimap_scalar_sine(std::uint16_t(a+0x4000));
    return {{float(c*p[0]-s*p[1]),float(s*p[0]+c*p[1])}};
}
std::array<MapPoint,4> rectangle(MapPoint center, float x, float top, float bottom,
                                 std::uint16_t angle) noexcept {
    std::array<MapPoint,4> p{{{{x,top}},{{-x,top}},{{-x,-bottom}},{{x,-bottom}}}};
    for(auto& v:p) { v=rotate(v,angle); v[0]=float(double(v[0])+center[0]); v[1]=float(double(v[1])+center[1]); }
    return p;
}
bool inside(MapPoint p,const std::array<MapPoint,4>& polygon) noexcept {
    bool hit=false;
    for(std::size_t i=0,j=3;i<4;j=i++) {
        const auto a=polygon[i],b=polygon[j];
        if((a[1]<=p[1] && p[1]<b[1]) || (b[1]<=p[1] && p[1]<a[1])) {
            const double cross=double(a[0])+(double(p[1])-a[1])*(double(b[0])-a[0])/(double(b[1])-a[1]);
            if(cross>p[0]) hit=!hit;
        }
    }
    return hit;
}
MinimapVertex interpolate(const MinimapVertex& a,const MinimapVertex& b,double t) noexcept {
    MinimapVertex v;
    for(int k=0;k<2;++k) v.screen[k]=float(double(a.screen[k])+(double(b.screen[k])-a.screen[k])*t);
    for(int k=0;k<3;++k) v.uvq[k]=float(double(a.uvq[k])+(double(b.uvq[k])-a.uvq[k])*t);
    for(int k=0;k<4;++k) v.color[k]=std::uint8_t(int(double(a.color[k])+(int(b.color[k])-int(a.color[k]))*t));
    return v;
}
// Native clips an ordered polygon and emits a fan of four-vertex pieces,
// repeating the last vertex for a final triangle (0x0059090F onward).
MinimapResult clip(MinimapPiece piece,const MinimapPresentation& p,MinimapGeometry& out,
           std::size_t capacity) noexcept {
    std::array<MinimapVertex,12> a{},b{};
    std::array<float,4> bounds{};
    for(int edge=0;edge<4;++edge) {
        const int axis=(edge==0 || edge==2)?1:0;
        bounds[edge]=p.native_center[axis]+(edge<2?-p.native_half_extent[axis]:p.native_half_extent[axis]);
        if(!std::isfinite(bounds[edge])) return MinimapResult::invalid_input;
    }
    for(const auto& v:piece.vertices) if(!finite_vertex(v)) return MinimapResult::invalid_input;
    std::copy(piece.vertices.begin(),piece.vertices.end(),a.begin());
    std::size_t count=4;
    for(int edge=0;edge<4 && count; ++edge) {
        const int axis=(edge==0 || edge==2)?1:0;
        const bool lower=edge<2;
        const float bound=bounds[edge];
        std::size_t n=0;
        auto prev=a[count-1]; bool prev_in=lower?prev.screen[axis]>=bound:prev.screen[axis]<=bound;
        for(std::size_t i=0;i<count;++i) {
            const auto cur=a[i]; const bool cur_in=lower?cur.screen[axis]>=bound:cur.screen[axis]<=bound;
            if(cur_in!=prev_in) {
                if(n==b.size()) return MinimapResult::capacity_exceeded;
                const double t=(double(bound)-prev.screen[axis])/(double(cur.screen[axis])-prev.screen[axis]);
                if(!std::isfinite(t) || t<0 || t>1) return MinimapResult::invalid_input;
                b[n]=interpolate(prev,cur,t); b[n++].screen[axis]=bound;
                if(!finite_vertex(b[n-1])) return MinimapResult::invalid_input;
            }
            if(cur_in) { if(n==b.size()) return MinimapResult::capacity_exceeded; b[n++]=cur; }
            prev=cur; prev_in=cur_in;
        }
        a=b; count=n;
    }
    if(count<3) return MinimapResult::ok;
    // 0x00590830: native fan starts at least y, breaking near-equal y
    // by x. This matters for affine marker UV interpolation under tilt.
    std::size_t first=0;
    for(std::size_t i=1;i<count;++i) {
        if(a[i].screen[1]<a[first].screen[1] ||
           (std::abs(double(a[i].screen[1])-a[first].screen[1])<=0.001F &&
            a[i].screen[0]<a[first].screen[0])) first=i;
    }
    b=a;
    for(std::size_t i=0;i<count;++i) a[i]=b[(first+i)%count];
    for(std::size_t i=1;i+1<count;i+=2) {
        if(out.count==capacity) return MinimapResult::capacity_exceeded;
        piece.vertices={{a[0],a[i],a[i+1],a[std::min(i+2,count-1)]}};
        for(auto& v:piece.vertices) {
            const double x=v.screen[0],y=v.screen[1]; const auto& t=p.screen_transform;
            v.screen={{float(t[0]*x+t[1]*y+t[2]),float(t[3]*x+t[4]*y+t[5])}};
            if(!finite_vertex(v)) return MinimapResult::invalid_input;
        }
        out.pieces[out.count++]=piece;
    }
    return MinimapResult::ok;
}
bool screen_quad(const std::array<MapPoint,4>& local,const std::array<MapPoint,4>& uv,
    const MinimapStyle& s,const MinimapPresentation& p,std::array<std::uint8_t,3> color,bool route,
    MinimapPiece& piece) noexcept {
    piece={}; piece.route=route;
    const double sin=minimap_scalar_sine(s.tilt),cos=minimap_scalar_sine(std::uint16_t(s.tilt+0x4000));
    const float anchor_x=p.native_center[0]+s.offset[0],anchor_y=p.native_center[1]+s.offset[1];
    const float ey=p.native_half_extent[1]+s.extent_add[1];
    for(std::size_t i=0;i<4;++i) {
        auto& v=piece.vertices[i];
        const float y=float(cos*local[i][1]),z=float(sin*local[i][1]);
        const double denom=double(s.distance)+z;
        if(!finite(local[i]) || !finite(uv[i]) || !std::isfinite(denom) || denom<=0) return false;
        v.screen={{float(double(s.distance)*local[i][0]/denom+anchor_x),float(double(s.distance)*y/denom+anchor_y)}};
        const double q=1.0/denom;
        // Route carries perspective UVQ; marker UVQ remains affine in native.
        v.uvq=route?std::array<float,3>{{float(uv[i][0]*q),float(uv[i][1]*q),float(q)}}:
                     std::array<float,3>{{uv[i][0],uv[i][1],1}};
        const double alpha_value=z>0?(1.0-double(z)/(double(ey)*float(0.6666666865348816)))*128:128;
        if(!finite_vertex(v) || !std::isfinite(alpha_value) || alpha_value< -2147483648.0 || alpha_value>2147483647.0) return false;
        const int alpha=int(alpha_value);
        const auto byte=std::uint8_t(alpha);
        v.color={{color[0],color[1],color[2],route?std::uint8_t(int(double(byte)*s.route_alpha)):byte}};
    }
    return true;
}
}

float minimap_scalar_sine(std::uint16_t angle) noexcept {
    double x=double(angle)*float(9.58738019107841e-05),sign=1;
    if(x>=float(4.71238899230957)) x-=float(6.2831854820251465);
    else if(x>=float(1.5707963705062866)) { x-=float(3.1415927410125732); sign=-1; }
    const double x2=x*x,x3=x2*x,x5=x3*x2,x7=x5*x2,x9=x7*x2;
    return float(sign*(x-x3*float(0.1666666716337204)+x5*float(0.008333333767950535)-
                       x7*float(0.00019841270113829523)+x9*float(2.7557318844628753e-06)));
}
bool load_minimap_resource(const std::filesystem::path& root,MinimapResource& out,std::string& error) {
    out={}; error.clear();
    std::vector<unsigned char> bytes;
    const auto packs=load_texture_pack_set(root/"Tracks/TEX1003TRACK.BIN",bytes);
    if(!packs.complete || !packs.ok() || sha256_hex(sha256(bytes.data(),bytes.size()))!=
        "081B4D966C154D7B9823E70DD5031269A8D3293F8CD13ED515EF644F09573ED5") {
        error="unsupported TRACK1003 texture pack"; return false;
    }
    MinimapResource r; unsigned matches=0;
    for(const auto& pack:packs.packs) for(const auto& t:pack.textures) if(t.hash==0xA4F2F595) {
        if(!t.valid || t.name!="TRACK1003_MAP" || t.width!=256 || t.height!=256 ||
           t.format!=PixelFormat::a8r8g8b8 || t.storage!=TextureStorage::raw || t.mip_count!=1 ||
           t.blob_offset>bytes.size() || t.blob_size>bytes.size()-t.blob_offset) {
            error="unsupported TRACK1003_MAP metadata"; return false;
        }
        ++matches; r.hash=t.hash; r.width=t.width; r.height=t.height;
        r.info_offset=t.info_offset; r.payload_offset=t.blob_offset; r.payload_bytes=t.blob_size;
        r.alpha_test=t.alpha_test_mode; r.alpha_blend=t.alpha_blend_mode;
    }
    if(matches!=1) { error="ambiguous or absent TRACK1003_MAP"; return false; }
    std::ifstream stream(root/"Global/GLOBALB.BUN",std::ios::binary|std::ios::ate);
    if(!stream || stream.tellg()<0 || stream.tellg()>64*1024*1024) { error="GLOBALB read refused"; return false; }
    bytes.resize(static_cast<std::size_t>(stream.tellg())); stream.seekg(0);
    if(!stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())) ||
       sha256_hex(sha256(bytes.data(),bytes.size()))!="39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9") {
        error="unsupported GLOBALB identity"; return false;
    }
    const auto table=parse_track_info_table(bytes.data(),bytes.size());
    if(!table.valid) { error="invalid track table"; return false; }
    matches=0;
    for(const auto& t:table.records) if(t.number==1003) {
        if(t.offset>bytes.size() || kTrackInfoRecordSize>bytes.size()-t.offset) { error="track record outside input"; return false; }
        const auto* at=bytes.data()+t.offset; ++matches;
        r.offset_x=f32(at+0x8c); r.offset_y=f32(at+0x90); r.world_span=f32(at+0x94);
        r.rotation=u16(at+0x98); r.start_angle=u16(at+0x9a); r.finish_angle=u16(at+0x9c);
    }
    if(matches!=1 || !finite({{r.offset_x,r.offset_y}}) || !(r.world_span>0) || !std::isfinite(r.world_span)) {
        error="invalid track1003 map transform"; return false;
    }
    r.valid=true; out=r; return true;
}
bool project_minimap_world(const MinimapResource& r,MapPoint xy,MapPoint& uv) noexcept {
    uv={}; if(!r.valid || !finite(xy) || !finite({{r.offset_x,r.offset_y}}) || !(r.world_span>0) || !std::isfinite(r.world_span)) return false;
    const auto q=rotate({{xy[0],-xy[1]}},r.rotation);
    const float x=float(double(q[0])-r.offset_x),y=float(double(q[1])+r.offset_y);
    uv={{x/r.world_span,y/r.world_span}}; return finite(uv);
}
MinimapResult build_minimap_geometry(const MinimapResource& r,const MinimapStyle& s,
    const MinimapPresentation& p,MapPoint view,const MinimapActor* actors,std::size_t count,
    MinimapGeometry& out,std::size_t capacity) noexcept {
    out={};
    if(!r.valid || r.hash!=0xA4F2F595 || r.width!=256 || r.height!=256) return MinimapResult::invalid_resource;
    if(!p.valid || !p.camera_angle_valid || (s.index!=1 && s.index!=3 && s.index!=5 && s.index!=6)) return MinimapResult::unknown_input;
    if(count>kMinimapMaxActors || capacity>kMinimapMaxPieces) return MinimapResult::capacity_exceeded;
    if((count && !actors) || !finite(view) || !finite(p.native_center) || !finite(p.native_half_extent) ||
       p.native_half_extent[0]<=0 || p.native_half_extent[1]<=0) return MinimapResult::invalid_input;
    for(float t:p.screen_transform) if(!std::isfinite(t) || std::abs(t)>1000000) return MinimapResult::invalid_input;
    if(!s.enabled) return s.index==6?MinimapResult::ok:MinimapResult::unknown_input;
    for(std::size_t i=0;i<count;++i) {
        if(!actors[i].raw_gates_valid) return MinimapResult::unknown_input;
        if(!finite(actors[i].raw_xy) || !std::isfinite(actors[i].size_x) || !std::isfinite(actors[i].size_y) ||
           actors[i].size_x<=0 || actors[i].size_y<=0 || actors[i].size_x>1 || actors[i].size_y>1) return MinimapResult::invalid_input;
        if(actors[i].texture_hash!=0x135663D6 && actors[i].texture_hash!=0x7E22DED8 &&
           actors[i].texture_hash!=0x72974A14) return MinimapResult::unknown_input;
        for(std::size_t j=0;j<i;++j) if(actors[j].id==actors[i].id) return MinimapResult::invalid_input;
    }
    if(!finite(s.offset) || !finite(s.extent_add) || !finite(s.uv_extent) || s.uv_extent[0]<=0 || s.uv_extent[1]<=0 ||
       !std::isfinite(s.distance) || s.distance<=0 || !std::isfinite(s.route_alpha) || s.route_alpha<0 || s.route_alpha>1)
        return MinimapResult::invalid_input;
    const float ex=p.native_half_extent[0]+s.extent_add[0],ey=p.native_half_extent[1]+s.extent_add[1];
    if(ex<=0 || ey<=0 || !std::isfinite(ex) || !std::isfinite(ey) ||
       double(s.distance)<=std::abs(double(minimap_scalar_sine(s.tilt))*ey)) return MinimapResult::invalid_input;
    MinimapGeometry frame; MapPoint uv{};
    if(!project_minimap_world(r,view,uv)) return MinimapResult::invalid_input;
    const float top=s.uv_extent[0],bottom=s.uv_extent[1],half=float((double(top)+bottom)*0.5);
    auto route_uv=s.centered?rectangle(uv,half,top,bottom,p.map_angle):
        std::array<MapPoint,4>{{{{0,0}},{{1,0}},{{1,1}},{{0,1}}}};
    frame.center={{float((double(route_uv[0][0])+route_uv[2][0])*0.5),float((double(route_uv[0][1])+route_uv[2][1])*0.5)}};
    frame.boundary=s.centered?rectangle(uv,float(double(half)*float(1.2000000476837158)),
        float(double(top)*float(1.2000000476837158)),float(double(bottom)*float(1.2000000476837158)),p.map_angle):route_uv;
    if(!finite_polygon(route_uv) || !finite(frame.center) || !finite_polygon(frame.boundary)) return MinimapResult::invalid_input;
    const std::array<MapPoint,4> local{{{{-ex,-ey}},{{ex,-ey}},{{ex,ey}},{{-ex,ey}}}};
    MinimapPiece route;
    if(!screen_quad(local,route_uv,s,p,{{128,128,128}},true,route)) return MinimapResult::invalid_input;
    route.kind=14; route.texture_hash=r.hash;
    const auto route_result=clip(route,p,frame,capacity);
    if(route_result!=MinimapResult::ok) return route_result;
    for(std::size_t i=0;i<count;++i) {
        const auto& a=actors[i]; if(!a.gate_4d0 || !a.gate_8a9) continue;
        MapPoint marker_uv{}; if(!project_minimap_world(r,a.raw_xy,marker_uv)) return MinimapResult::invalid_input;
        if(!inside(marker_uv,frame.boundary)) continue;
        auto delta=rotate({{float(double(marker_uv[0])-frame.center[0]),float(double(marker_uv[1])-frame.center[1])}},std::uint16_t(0-p.map_angle));
        if(s.centered) { delta[0]=float(double(delta[0])/(double(top)+bottom)); delta[1]=float(double(delta[1])/(double(top)+bottom)); }
        const auto heading=s.heading_aware?std::uint16_t(r.rotation-a.heading-p.map_angle+0x4000):std::uint16_t(0);
        auto corners=rectangle(delta,a.size_x*0.5F,a.size_y*0.5F,a.size_y*0.5F,heading);
        for(auto& v:corners) { v[0]=float(-double(v[0])*float(double(ex)*2)); v[1]=float(-double(v[1])*float(double(ey)*2)); }
        MinimapPiece piece;
        if(!screen_quad(corners,{{{{0,0}},{{1,0}},{{1,1}},{{0,1}}}},s,p,a.color,false,piece)) return MinimapResult::invalid_input;
        piece.actor_id=a.id; piece.kind=a.kind; piece.texture_hash=a.texture_hash;
        const auto marker_result=clip(piece,p,frame,capacity);
        if(marker_result!=MinimapResult::ok) return marker_result;
    }
    out=frame; return MinimapResult::ok;
}
} // namespace nfsu
