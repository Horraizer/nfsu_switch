#include "nfsu/custom_vinyl.h"
#include "nfsu/scene.h"
#include "nfsu/texture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace nfsu {
namespace {
#include "kr_logo.inc"
struct Logo {unsigned width=0,height=0;std::vector<unsigned char> rgba;};
const Logo& kr_logo() {
    static const Logo logo=[] {
        const std::string alphabet="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%&()*+-;<=>?@^_`{|}~";
        std::vector<unsigned char> bytes;bytes.reserve(sizeof(kKrLogo)*4/5);
        for(std::size_t at=0;at<sizeof(kKrLogo)-1;at+=5) {
            std::uint64_t value=0;
            for(unsigned k=0;k<5;++k){const auto digit=alphabet.find(kKrLogo[at+k]);if(digit==std::string::npos)throw std::runtime_error("KR asset alphabet");value=value*85+digit;}
            if(value>0xffffffffULL)throw std::runtime_error("KR asset word");
            for(unsigned k=0;k<4;++k)bytes.push_back(static_cast<unsigned char>(value>>(24-8*k)));
        }
        const auto word=[&](std::size_t at,unsigned count){if(at+count>bytes.size())throw std::runtime_error("KR asset truncated");std::uint32_t value=0;for(unsigned k=0;k<count;++k)value|=std::uint32_t(bytes[at+k])<<(8*k);return value;};
        Logo out;out.width=word(0,4);out.height=word(4,4);const auto colors=word(8,4),runs=word(12,4);
        if(!out.width||!out.height||out.width>2048||out.height>2048||colors>65535)throw std::runtime_error("KR asset dimensions");
        const auto count=std::size_t(out.width)*out.height;out.rgba.resize(count*4);std::size_t pixel=0;
        for(unsigned i=0;i<runs;++i){const auto at=16+std::size_t(colors)*4+std::size_t(i)*4;const auto length=word(at,2),index=word(at+2,2);
            if(!length||length>count-pixel||index>=colors)throw std::runtime_error("KR asset run");
            const auto color=word(16+4*index,4);for(unsigned j=0;j<length;++j,++pixel)for(unsigned k=0;k<4;++k)out.rgba[pixel*4+k]=static_cast<unsigned char>(color>>(8*k));
        }
        if(pixel!=count)throw std::runtime_error("KR asset incomplete");return out;
    }();return logo;
}
using Point=std::array<double,3>;
Point point(const MeshVertex& v,const PreparedInstance& instance) {
    const double p[]={v.px,v.pz,v.py};Point out{};
    for(unsigned k=0;k<3;++k){out[k]=instance.transform[12+k];for(unsigned j=0;j<3;++j)out[k]+=p[j]*instance.transform[4*j+k];}return out;
}
Point cross(const Point& a,const Point& b){return {{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}};}
Point subtract(const Point& a,const Point& b){return {{a[0]-b[0],a[1]-b[1],a[2]-b[2]}};}
struct Bounds {Point low{{1e20,1e20,1e20}},high{{-1e20,-1e20,-1e20}};bool valid=false;
 void add(const Point& p){valid=true;for(unsigned k=0;k<3;++k){low[k]=std::min(low[k],p[k]);high[k]=std::max(high[k],p[k]);}}
};
struct DoorFit {double x=0,z=0,size=0;};
std::array<DoorFit,2> fit_doors(const PreparedCarScene& scene,const Bounds& body,
                              std::uint32_t body_hash,std::uint32_t target_hash) {
    const double cell=std::max((body.high[0]-body.low[0])/128.,(body.high[2]-body.low[2])/96.);
    if(!(cell>0))throw std::runtime_error("KR side bounds");
    const unsigned width=unsigned(std::ceil((body.high[0]-body.low[0])/cell))+1;
    const unsigned height=unsigned(std::ceil((body.high[2]-body.low[2])/cell))+1;
    const double middle=(body.low[1]+body.high[1])*.5;
    std::array<std::vector<unsigned char>,2> mask;
    for(auto& side:mask)side.resize(std::size_t(width)*height);
    for(const auto& instance:scene.instances)if(instance.part_slot==5||instance.part_slot==6) {
        const auto& mesh=scene.meshes.at(instance.mesh);
        for(const auto& draw:scene.draws)if(draw.mesh==instance.mesh&&draw.drawable&&(draw.texture_hash==body_hash||draw.texture_hash==target_hash))
        for(unsigned index=draw.first_index;index+2<draw.first_index+draw.index_count;index+=3) {
            const auto& triangle=mesh.triangles.at(index/3);
            const Point p[]={point(mesh.vertices.at(triangle.a),instance),point(mesh.vertices.at(triangle.b),instance),point(mesh.vertices.at(triangle.c),instance)};
            const auto normal=cross(subtract(p[1],p[0]),subtract(p[2],p[0]));
            const double length=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
            const double lateral=(p[0][1]+p[1][1]+p[2][1])/3.;
            if(length<1e-9||std::abs(normal[1])<.5*length||std::abs(lateral-middle)<(body.high[1]-body.low[1])*.3)continue;
            double u[3],v[3];for(unsigned k=0;k<3;++k){u[k]=(p[k][0]-body.low[0])/cell;v[k]=(p[k][2]-body.low[2])/cell;}
            const double d=(v[1]-v[2])*(u[0]-u[2])+(u[2]-u[1])*(v[0]-v[2]);if(std::abs(d)<1e-9)continue;
            const int left=std::max(0,int(std::floor(std::min({u[0],u[1],u[2]})))),right=std::min(int(width)-1,int(std::ceil(std::max({u[0],u[1],u[2]}))));
            const int low=std::max(0,int(std::floor(std::min({v[0],v[1],v[2]})))),high=std::min(int(height)-1,int(std::ceil(std::max({v[0],v[1],v[2]}))));
            auto& side=mask[lateral>middle?1:0];
            for(int y=low;y<=high;++y)for(int x=left;x<=right;++x){
                const double a=((v[1]-v[2])*(x+.5-u[2])+(u[2]-u[1])*(y+.5-v[2]))/d;
                const double b=((v[2]-v[0])*(x+.5-u[2])+(u[0]-u[2])*(y+.5-v[2]))/d;
                if(a>=-1e-7&&b>=-1e-7&&a+b<=1.0000001)side[std::size_t(y)*width+unsigned(x)]=1;
            }
        }
    }
    std::array<DoorFit,2> out;
    for(unsigned side=0;side<2;++side) {
        std::vector<unsigned> previous(width),current(width);double best=-1;
        const double desired=(body.low[0]+body.high[0])*.5;
        for(unsigned y=0;y<height;++y){
            for(unsigned x=0;x<width;++x){
                current[x]=mask[side][std::size_t(y)*width+x]?1U+(x&&y?std::min({current[x-1],previous[x],previous[x-1]}):0U):0U;
                const unsigned size=current[x];if(size<=4)continue;
                const double cx=body.low[0]+(double(x)+1-double(size)*.5)*cell;
                // Prefer the central door area over small bumper/quarter patches.
                if(std::abs(cx-desired)>(body.high[0]-body.low[0])*.2)continue;
                const double score=double(size)-.1*std::abs(cx-desired)/cell;
                if(score>best){best=score;out[side]={cx,body.low[2]+(double(y)+1-double(size)*.5)*cell,(double(size)-2)*cell*.94};}
            }
            current.swap(previous);
        }
        if(!(out[side].size>.1))throw std::runtime_error("KR painted side panel unavailable");
    }
    return out;
}
void blend_logo(const Logo& logo,double u,double v,unsigned char* destination) {
    if(u<0||u>1||v<0||v>1)return;
    const double x=u*(logo.width-1),y=v*(logo.height-1);const unsigned ix=unsigned(x),iy=unsigned(y);
    const double fx=x-ix,fy=y-iy;double color[4]{};
    for(unsigned j=0;j<2;++j)for(unsigned i=0;i<2;++i){const auto offset=(std::min(iy+j,logo.height-1)*std::size_t(logo.width)+std::min(ix+i,logo.width-1))*4;const double weight=(i?fx:1-fx)*(j?fy:1-fy);for(unsigned k=0;k<4;++k)color[k]+=weight*logo.rgba[offset+k];}
    const double alpha=color[3]/255.;for(unsigned k=0;k<3;++k)destination[k]=static_cast<unsigned char>(std::clamp(std::lround(color[k]*alpha+destination[k]*(1-alpha)),0L,255L));
    // The generated body's alpha stores paint/gloss information, not decal coverage.
}
}
bool apply_kr_vinyl(const PreparedCarScene& scene,const std::string& car,unsigned width,unsigned height,
                    std::vector<unsigned char>& rgba,std::string& error) {
 try {
    if(!width||!height||width>2048||height>2048||rgba.size()!=std::size_t(width)*height*4){error="KR skin dimensions";return false;}
    const auto& logo=kr_logo();Bounds body,hood;
    for(const auto& instance:scene.instances)if(instance.part_slot==5||instance.part_slot==6||instance.part_slot==8) {
        const auto& mesh=scene.meshes.at(instance.mesh);for(const auto& vertex:mesh.vertices)(instance.part_slot==8?hood:body).add(point(vertex,instance));
    }
    if(!body.valid){error="KR body geometry unavailable";return false;}
    const auto body_hash=bin_hash(car+"_SKIN1"),target_hash=bin_hash("DUMMY_SKIN1");
    const auto doors=fit_doors(scene,body,body_hash,target_hash);std::vector<unsigned char> painted(std::size_t(width)*height);
    for(const auto& instance:scene.instances)if(instance.part_slot==5||instance.part_slot==6||instance.part_slot==8) {
        const bool top=instance.part_slot==8;const auto& mesh=scene.meshes.at(instance.mesh);
        for(const auto& draw:scene.draws)if(draw.mesh==instance.mesh&&draw.drawable&&(draw.texture_hash==body_hash||draw.texture_hash==target_hash))
        for(unsigned index=draw.first_index;index+2<draw.first_index+draw.index_count;index+=3) {
            const auto& triangle=mesh.triangles.at(index/3);const MeshVertex* vertices[]={&mesh.vertices.at(triangle.a),&mesh.vertices.at(triangle.b),&mesh.vertices.at(triangle.c)};
            Point positions[3];for(unsigned k=0;k<3;++k)positions[k]=point(*vertices[k],instance);
            const auto normal=cross(subtract(positions[1],positions[0]),subtract(positions[2],positions[0]));const double length=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
            if(length<1e-9||(top?std::abs(normal[2]):std::abs(normal[1]))<.5*length)continue;
            double u[3],v[3];for(unsigned k=0;k<3;++k){u[k]=vertices[k]->u*width;v[k]=vertices[k]->v*height;}
            const double denominator=(v[1]-v[2])*(u[0]-u[2])+(u[2]-u[1])*(v[0]-v[2]);if(std::abs(denominator)<1e-8)continue;
            const int left=std::max(0,int(std::floor(std::min({u[0],u[1],u[2]})))),right=std::min(int(width)-1,int(std::ceil(std::max({u[0],u[1],u[2]}))));
            const int first=std::max(0,int(std::floor(std::min({v[0],v[1],v[2]})))),last=std::min(int(height)-1,int(std::ceil(std::max({v[0],v[1],v[2]}))));
            const auto& bounds=top?hood:body;
            const double size=top?std::min((bounds.high[0]-bounds.low[0])*.75,(bounds.high[1]-bounds.low[1])*.7):1.;
            if(size<.1)continue;
            const double cx=(bounds.low[0]+bounds.high[0])*.5,cy=(bounds.low[1]+bounds.high[1])*.5;

            for(int y=first;y<=last;++y)for(int x=left;x<=right;++x) {
                const auto pixel=std::size_t(y)*width+unsigned(x);if(painted[pixel])continue;
                const double a=((v[1]-v[2])*(x+.5-u[2])+(u[2]-u[1])*(y+.5-v[2]))/denominator;
                const double b=((v[2]-v[0])*(x+.5-u[2])+(u[0]-u[2])*(y+.5-v[2]))/denominator,c=1-a-b;
                if(a< -1e-7||b< -1e-7||c< -1e-7)continue;
                Point p{};for(unsigned k=0;k<3;++k)p[k]=a*positions[0][k]+b*positions[1][k]+c*positions[2][k];
                if(!top&&std::abs(p[1]-cy)<(bounds.high[1]-bounds.low[1])*.3)continue;
                const auto& door=doors[p[1]>cy?1:0];
                const double tx=top?(p[1]-cy)/size+.5:(p[0]-door.x)*(p[1]>cy?-1:1)/door.size+.5;
                const double ty=top?(p[0]-cx)/size+.5:(door.z-p[2])/door.size+.5;
                if(tx<0||tx>1||ty<0||ty>1)continue;
                blend_logo(logo,tx,ty,rgba.data()+pixel*4);painted[pixel]=1;
            }
        }
    }
    error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}
}
} // namespace nfsu
