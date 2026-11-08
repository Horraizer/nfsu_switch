#include "nfsu/start_crowd.h"
#include "nfsu/eagl_animation.h"
#include "nfsu/texture_decode.h"
#include "nfsu/geometry.h"
#include "nfsu/track.h"
#include "nfsu/drive_session.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/track_ground.h"
#include <functional>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <tuple>
#include <fstream>
#include <stdexcept>

namespace nfsu {
namespace {
using Bytes=std::vector<unsigned char>;
using Matrix=std::array<float,16>;
using eagl::Pose;
using eagl::View;
using eagl::Elf;
using eagl::channel;
void need(bool ok,const char* why) { if(!ok)throw std::runtime_error(why); }
std::size_t align(std::size_t n,std::size_t a){return (n+a-1)&~(a-1);}
std::uint32_t hash(const std::string& s){std::uint32_t h=0xffffffffU;for(unsigned char c:s)h=h*33+c;return h;}
Bytes read_crowd_file(FileSystemService& fs,const std::filesystem::path& path,std::size_t limit) {
    std::uint64_t n=0;std::string e;need(fs.file_size(path,n,e),"crowd file unavailable");
    need(n && n<=limit,"crowd input size limit");Bytes b(static_cast<std::size_t>(n));std::size_t got=0;
    need(fs.read_file(path,0,b.data(),b.size(),got,e)&&got==b.size(),"crowd input read failed");return b;
}
struct Chunk {std::uint32_t id;std::size_t at,end;};
std::vector<Chunk> chunks(View b,std::size_t first,std::size_t end,bool recurse=false) {
    b.range(first,end-first);std::vector<Chunk> out;std::vector<std::pair<std::size_t,std::size_t>> work{{first,end}};
    while(!work.empty()) {
        auto [a,z]=work.back();work.pop_back();
        while(a<z) {
            need(z-a>=8,"crowd chunk header");const auto id=b.u32(a),n=b.u32(a+4);need(n<=z-a-8,"crowd chunk length");
            out.push_back({id,a,a+8+n});need(out.size()<200000,"crowd chunk limit");
            if(recurse && (id&0x80000000U))work.push_back({a+8,a+8+n});
            a+=8+n;
        }
    }return out;
}
struct FileChunk {std::uint32_t id;std::uint64_t at,end;std::uint32_t mesh_hash=0;};
Bytes read_range(FileSystemService& fs,const std::filesystem::path& path,std::uint64_t at,std::size_t n) {
    need(n<=64*1024*1024,"start scene chunk size limit");Bytes b(n);std::size_t got=0;std::string error;
    need(fs.read_file(path,at,b.data(),n,got,error)&&got==n,"start camera chunk read");return b;
}
std::vector<FileChunk> file_chunks(FileSystemService& fs,const std::filesystem::path& path) {
    std::uint64_t length=0;std::string error;need(fs.file_size(path,length,error)&&length>=8,"start scene file absent");
    std::vector<FileChunk> out;
    // FileSystemService closes every read. A city has tens of thousands of
    // tiny headers; read ahead within this traversal instead of reopening
    // the SD file for each eight-byte header. No full city payload is held.
    constexpr std::size_t window_capacity=128*1024;
    Bytes window;std::uint64_t window_at=0,window_end=0;
    const auto peek=[&](std::uint64_t at,std::size_t count) {
        need(at<=length&&count<=length-at,"start scene metadata outside file");
        if(at<window_at || at+count>window_end) {
            const auto size=std::size_t(std::min<std::uint64_t>(window_capacity,length-at));
            window.resize(size);std::size_t got=0;
            need(fs.read_file(path,at,window.data(),size,got,error)&&got==size,"start scene header window read");
            window_at=at;window_end=at+size;
        }
        return View(window.data()+std::size_t(at-window_at),count);
    };
    std::function<void(std::uint64_t,std::uint64_t,unsigned)> walk;
    walk=[&](std::uint64_t at,std::uint64_t end,unsigned depth) {
        need(depth<=16,"start scene chunk nesting");
        while(at<end) {
            need(end-at>=8 && out.size()<200000,"start scene chunk header");
            const auto h=peek(at,8);const auto id=h.u32(0),size=h.u32(4);
            need(size<=end-at-8,"start scene chunk length");
            std::uint32_t mesh_hash=0;
            if(id==0x134011) {
                const auto header=(at+8+15)&~std::uint64_t(15);
                need(header+20<=at+8+size,"start scene solid header length");
                mesh_hash=peek(header,20).u32(16);
            }
            out.push_back({id,at+8,at+8+size,mesh_hash});
            if(id&0x80000000U)walk(at+8,at+8+size,depth+1);
            at+=8+size;
        }
    };walk(0,length,0);return out;
}
Matrix multiply(const Matrix& a,const Matrix& b) {
    Matrix r{};for(int i=0;i<4;++i)for(int j=0;j<4;++j)for(int k=0;k<4;++k)r[4*i+j]+=a[4*i+k]*b[4*k+j];return r;
}
Matrix inverse_affine(const Matrix& a) {
    const float det=a[0]*(a[5]*a[10]-a[6]*a[9])-a[1]*(a[4]*a[10]-a[6]*a[8])+a[2]*(a[4]*a[9]-a[5]*a[8]);
    need(std::isfinite(det) && std::abs(det)>1e-8F,"crowd singular bind pose");
    Matrix r{{(a[5]*a[10]-a[6]*a[9])/det,(a[2]*a[9]-a[1]*a[10])/det,(a[1]*a[6]-a[2]*a[5])/det,0,
        (a[6]*a[8]-a[4]*a[10])/det,(a[0]*a[10]-a[2]*a[8])/det,(a[2]*a[4]-a[0]*a[6])/det,0,
        (a[4]*a[9]-a[5]*a[8])/det,(a[1]*a[8]-a[0]*a[9])/det,(a[0]*a[5]-a[1]*a[4])/det,0,0,0,0,1}};
    for(int k=0;k<3;++k)r[12+k]=-(a[12]*r[k]+a[13]*r[4+k]+a[14]*r[8+k]);
    return r;
}
Matrix pose_matrix(const Pose& p) {
    const float x=p[4],y=p[5],z=p[6],w=p[7];
    return {{(1-2*y*y-2*z*z)*p[0]*p[3],(2*x*y+2*z*w)*p[0],(2*x*z-2*y*w)*p[0],0,
        (2*x*y-2*z*w)*p[1]*p[3],(1-2*x*x-2*z*z)*p[1],(2*y*z+2*x*w)*p[1],0,
        (2*x*z+2*y*w)*p[2]*p[3],(2*y*z-2*x*w)*p[2],(1-2*x*x-2*y*y)*p[2],0,p[8],p[9],p[10],1}};
}
struct Skeleton {std::vector<Pose> pose;std::vector<Matrix> inverse;std::vector<int> parent;};
Skeleton skeleton(const Elf& e,std::size_t at) {
    const auto n=e.data.u32(at+8);need(n && n<=64,"crowd bone count");e.data.range(at+16,n*112);
    Skeleton s;s.pose.resize(n);s.inverse.resize(n);s.parent.resize(n);
    for(unsigned i=0;i<n;++i) {
        auto a=at+16+i*112;s.parent[i]=static_cast<std::int32_t>(e.data.u32(a+12));
        need(s.parent[i]>=-1 && s.parent[i]<int(i),"crowd bone parent order");
        for(int k=0;k<12;++k)s.pose[i][k]=k==3?1:e.data.f32(a+k*4);
        for(int k=0;k<16;++k)s.inverse[i][k]=e.data.f32(a+48+k*4);
    }
    std::vector<Matrix> bind(n);
    for(unsigned i=0;i<n;++i) {
        auto p=s.pose[i];for(unsigned k=8;k<11;++k)p[k]*=0.01F;
        bind[i]=pose_matrix(p);if(s.parent[i]>=0)bind[i]=multiply(bind[i],bind[std::size_t(s.parent[i])]);
        s.inverse[i]=inverse_affine(bind[i]);
    }
    return s;
}
void skin_pose(const std::vector<Pose>& pose,const Skeleton& s,std::vector<Matrix>& world,std::vector<Matrix>& skin) {
    for(std::size_t i=0;i<pose.size();++i) {
        world[i]=pose_matrix(pose[i]);if(s.parent[i]>=0)world[i]=multiply(world[i],world[std::size_t(s.parent[i])]);
        skin[i]=multiply(s.inverse[i],world[i]);
    }
}
std::vector<Pose> drift_car_motion(const std::filesystem::path& root,const std::string& region) {
    std::ifstream file(root/("Tracks/STREAM"+region+".BUN"),std::ios::binary|std::ios::ate);
    need(bool(file),"drift car stream unavailable");const auto end=std::uint64_t(file.tellg());
    bool selected=false;
    for(std::uint64_t at=0;at+8<=end;) {
        unsigned char h[8];file.seekg(at);need(bool(file.read(reinterpret_cast<char*>(h),8)),"drift car stream header");
        View head(h,8);const auto kind=head.u32(0),n=head.u32(4);need(n<=end-at-8,"drift car chunk bounds");
        if(kind==0x34035) {
            unsigned char id[8];need(n>=8 && bool(file.read(reinterpret_cast<char*>(id),8)),"drift car scene header");
            if(selected)break;selected=View(id,8).u32(4)==kFirstDriftStartScene;
        }else if(selected && kind==0xe34010) {
            need(n<1024*1024,"drift car bank size");Bytes data(n);
            need(bool(file.read(reinterpret_cast<char*>(data.data()),n)),"drift car bank read");
            const auto pad=align(std::size_t(at+8),16)-std::size_t(at+8);need(pad<n,"drift car ELF alignment");
            Elf e(View(data.data()+pad,n-pad));
            const auto found=e.symbols.find("__AnimationBank:::bank");need(found!=e.symbols.end(),"drift car bank symbol");
            const auto bank=found->second;const auto count=e.data.u32(bank+4),ptr=e.data.u32(bank+12),names=e.data.u32(bank+16);
            need(count<1024,"drift car bank count");std::array<std::size_t,2> channels{};
            for(unsigned i=0;i<count;++i) {
                const auto name=e.data.text(e.data.u32(names+4*i));
                if(name=="ZPM_Car1_q")channels[0]=e.data.u32(ptr+4*i);
                if(name=="ZPM_Car1_t")channels[1]=e.data.u32(ptr+4*i);
            }
            if(!channels[0] || !channels[1]){at+=8+n;continue;}
            const auto frames=e.data.word(channels[0]+10);
            need(frames>0 && frames<=1024 && frames==e.data.word(channels[1]+10),"drift car motion length");
            std::vector<Pose> result;result.reserve(frames);
            for(unsigned f=0;f<frames;++f) {
                std::vector<Pose> pose(2);for(auto& p:pose)p={{1,1,1,1,0,0,0,1,0,0,0,0}};
                for(auto a:channels){need(e.data.word(a+8)==1,"drift car channels");channel(e.data,e.data.u32(a+12),f,pose);}
                result.push_back(pose[1]);
            }
            return result;
        }
        at+=8+n;
    }
    throw std::runtime_error("drift start car motion absent");
}
Matrix start_anchor(const VehicleTrackCourse& course) {
    const TrackMarker* marker=nullptr;
    for(const auto& m:course.track.markers)if(m.name_hash==hash(course.route.reverse?"BackwardStartLine":"ForwardStartLine") && m.param==0){marker=&m;break;}
    need(marker!=nullptr,"start line marker absent");
    const float angle=float(vehicle_track_heading(marker->angle)),cs=std::cos(angle),sn=std::sin(angle);
    //47CAC0 supplies XY only;47CB50 initializes query Z to0 and asks440A40
    // for its ordered road height/normal. Marker Z is not a search window:
    //1102 time-trial starts lie outside the former +2/-8 column.
    TrackGroundCache cache;TrackContact contact;
    need(query_track_ground(course.world,{marker->position[0],marker->position[1],0},cache,contact),"start line road absent");
    const auto& n=contact.normal;const float dot=cs*n[0]+sn*n[1];
    float r[3]={cs-dot*n[0],sn-dot*n[1],-dot*n[2]};const float length=std::sqrt(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]);
    need(length>.001F,"start line basis degenerate");for(auto& v:r)v/=length;
    const float f[3]={n[1]*r[2]-n[2]*r[1],n[2]*r[0]-n[0]*r[2],n[0]*r[1]-n[1]*r[0]};
    return {{r[0],r[1],r[2],0,f[0],f[1],f[2],0,n[0],n[1],n[2],0,marker->position[0],marker->position[1],contact.point[2],1}};
}
struct SkinVertex {MeshVertex vertex;std::array<float,3> weight;std::array<unsigned,3> bone;};
struct CrowdMesh {std::vector<SkinVertex> vertices;std::vector<std::uint16_t> indices;std::vector<MeshMaterial> materials;std::vector<std::uint32_t> textures;};
CrowdMesh mesh(View b,Chunk root) {
    CrowdMesh out;std::map<unsigned,Chunk> parts;
    for(const auto& c:chunks(b,root.at+8,root.end,true))parts[c.id]=c;
    const auto part=[&](unsigned id)->Chunk {need(parts.count(id)!=0,"crowd mesh chunk missing");return parts.at(id);};
    const auto counts=align(part(0x134900).at+8,16);b.range(counts,64);
    need(track_vertex_format(b.u32(counts+12)).stride==60,"crowd requires skinned vertex format");
    const auto nv=b.u32(counts+52),nf=b.u32(counts+36),nm=b.u32(counts+16);
    need(nv>0 && nv<=65536 && nf>0 && nf<65536 && nm>0 && nm<=256,"crowd mesh counts");
    const auto vc=part(0x134b01);auto a=align(vc.at+8,128);need(a+nv*60<=vc.end,"crowd vertex buffer length");
    out.vertices.resize(nv);
    for(auto& v:out.vertices) {
        v.vertex.px=b.f32(a);v.vertex.pz=b.f32(a+4);v.vertex.py=b.f32(a+8);
        v.vertex.nx=b.f32(a+12);v.vertex.nz=b.f32(a+16);v.vertex.ny=b.f32(a+20);
        std::copy_n(b.p+a+24,4,v.vertex.color);v.vertex.u=b.f32(a+28);v.vertex.v=b.f32(a+32);
        float sum=0;for(unsigned k=0;k<3;++k) {v.weight[k]=b.f32(a+36+4*k);sum+=v.weight[k];if(v.weight[k]==0){v.bone[k]=0;continue;}const float index=b.f32(a+48+4*k);need(index>=0 && index<64 && index==std::floor(index),"crowd skin bone");v.bone[k]=unsigned(index);}
        need(std::abs(sum-1)<0.001F,"crowd skin weight sum");a+=60;
    }
    const auto ic=part(0x134b03);a=align(ic.at+8,16);need(a+nf*6<=ic.end,"crowd indices length");
    for(unsigned i=0;i<nf*3;++i) {auto index=b.word(a+2*i);need(index<nv,"crowd index outside mesh");out.indices.push_back(std::uint16_t(index));}
    const auto tc=part(0x134012);for(a=tc.at+8;a<tc.end;a+=8)out.textures.push_back(b.u32(a));
    const auto mc=part(0x134b02);a=align(mc.at+8,16);need(a+nm*60<=mc.end,"crowd material length");
    unsigned covered=0;for(unsigned i=0;i<nm;++i,a+=60) {
        MeshMaterial m;std::copy_n(b.p+a,60,m.raw);m.first_index=b.u32(a+52);m.index_count=b.u32(a+12);m.texture_slot=b.u32(a+28);
        need(m.first_index==covered && m.index_count && m.index_count%3==0 && m.index_count<=nf*3-covered && m.texture_slot<out.textures.size(),"crowd material index run");
        covered+=m.index_count;out.materials.push_back(m);
    }need(covered==nf*3,"crowd incomplete material coverage");
    // Skin indices are local to each material's byte palette (+33), not
    // skeleton indices. A shared source vertex may have different palettes.
    const auto source=std::move(out.vertices);out.vertices.clear();
    for(const auto& m:out.materials) {
        std::map<unsigned,unsigned> remap;
        for(unsigned i=m.first_index;i<m.first_index+m.index_count;++i) {
            const auto old=out.indices[i];auto found=remap.find(old);unsigned index;
            if(found==remap.end()) {
                auto v=source[old];for(unsigned k=0;k<3;++k)if(v.weight[k]!=0) {
                    need(v.bone[k]<19,"crowd material palette index");v.bone[k]=m.raw[33+v.bone[k]];
                }
                index=unsigned(out.vertices.size());need(index<65536,"crowd expanded mesh count");remap[old]=index;out.vertices.push_back(v);
            }else index=found->second;
            out.indices[i]=std::uint16_t(index);
        }
    }return out;
}
void skin_mesh(const CrowdMesh& m,const std::vector<Matrix>& bones,std::vector<MeshVertex>& out) {
    out.clear();
    for(const auto& source:m.vertices) {
        auto v=source.vertex;const float p[]={v.px,v.pz,v.py},n[]={v.nx,v.nz,v.ny};float q[3]{},normal[3]{};
        for(unsigned j=0;j<3;++j)if(source.weight[j]!=0) {
            need(source.bone[j]<bones.size(),"crowd skin bone outside skeleton");const auto& b=bones[source.bone[j]];
            for(unsigned k=0;k<3;++k) {q[k]+=source.weight[j]*(p[0]*b[k]+p[1]*b[4+k]+p[2]*b[8+k]+b[12+k]);normal[k]+=source.weight[j]*(n[0]*b[k]+n[1]*b[4+k]+n[2]*b[8+k]);}
        }
        v.px=q[0];v.pz=q[1];v.py=q[2];v.nx=normal[0];v.nz=normal[1];v.ny=normal[2];out.push_back(v);
    }
}
}

struct StartCrowd::Data {
    struct Clip {
        CrowdMesh mesh;Skeleton skeleton;
        std::vector<std::vector<Pose>> frames;
        std::vector<Pose> pose;std::vector<Matrix> world,skin;
        std::vector<MeshVertex> vertices;
        std::size_t vertex=0,index=0;
        float speed=1;double sampled=-1;
    };
    struct Actor {Matrix local;std::size_t clip=0,first_draw=0,draws=0;float speed=1;};
    std::vector<Clip> clips;std::vector<Actor> actors;RenderTextureSet textures;RecordedCommands commands;
    std::size_t active=0,upload_base=0;std::uint64_t resident=0;
    Matrix anchor{};bool anchored=false;
    std::uint32_t post_go_frames=0, release_frame=0, go_frame=0;
    std::vector<Pose> camera_poses,car_poses;
    // Signed authored root travel, prepared once, sampled by the same clock.
    std::vector<double> car_travel;
    std::vector<std::size_t> camera_cuts;
};
StartCrowd::StartCrowd():data_(std::make_unique<Data>()){}
StartCrowd::~StartCrowd()=default;
const RecordedCommands& StartCrowd::commands()const{return data_->commands;}
std::size_t StartCrowd::draw_count()const{return data_->active;}
std::size_t StartCrowd::actor_count()const{return data_->actors.size();}
std::uint64_t StartCrowd::bytes()const{return data_->resident;}
void StartCrowd::rebase(std::size_t offset) {
    data_->upload_base+=offset;
    for(auto& d:data_->commands.owning_draws()) {d.vertex_resource+=offset;d.index_resource+=offset;d.texture_resource+=offset;}
    for(auto& c:data_->clips) {c.vertex+=offset;c.index+=offset;}
}
unsigned road_start_playlist(unsigned racers,unsigned grid) {
    if(!racers || racers>4 || !grid || grid>racers)throw std::runtime_error("start grid domain");
    if(racers==1)return 9;
    if(racers==2)return grid==1?7:8;
    if(racers==3)return grid==3?4:grid==1?5:6;
    return grid==3?0:grid==1?1:grid==2?2:3;
}
bool select_road_start_scene(FileSystemService& fs,const std::filesystem::path& root,
                            unsigned racers,unsigned grid,unsigned variant,unsigned& scene,std::string& error) {
    try {
        const auto list=road_start_playlist(racers,grid);const auto path=root/"Global/INGAMEB.BUN";
        bool found=false;unsigned selected=0;
        for(const auto& c:file_chunks(fs,path))if(c.id==0x34036) {
            need(!found,"duplicate start playlist");const auto bytes=read_range(fs,path,c.at,std::size_t(c.end-c.at));const View b(bytes);
            std::size_t offset=96;for(unsigned i=0;i<list;++i)offset+=4*b.u32(4*i);
            const auto count=b.u32(4*list);need(count && count<=64,"start playlist size");
            b.range(offset,4*count);selected=b.u32(offset+4*(variant%count));found=true;
        }
        need(found,"start playlist absent");scene=selected;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool StartCrowd::load_camera(FileSystemService& fs,const std::filesystem::path& root,unsigned scene,
                             const std::string& region,RaceStartClock& clock,std::string& error) {
    try {
        need(data_->actors.empty(),"start camera cannot replace crowd actors");
        need(region.size()==4 && std::all_of(region.begin(),region.end(),[](unsigned char c){return (c>='A'&&c<='Z')||(c>='0'&&c<='9');}),"start camera region");
        const auto path=root/("Tracks/STREAM"+region+".BUN");const auto parts=file_chunks(fs,path);
        auto next=std::make_unique<Data>();RaceStartClock timing;std::vector<unsigned> hashes;bool found=false;
        for(const auto& c:parts)if(c.id==0x34035 && c.end-c.at>=0x70) {
            const auto bytes=read_range(fs,path,c.at,0x70);const View b(bytes);if(b.u32(4)!=scene)continue;
            need(!found,"duplicate start camera scene");found=true;
            timing={b.u32(0x6c),b.u32(0x68),b.u32(0x64)};
            need(b.u32(0x60)==0 && timing.release && timing.go>timing.release && timing.end>=timing.go,"start camera timeline");
            const auto count=b.u32(8);need(count && count<=5,"start camera cut count");
            for(unsigned cut=0;cut<count;++cut)for(unsigned k=0;k<3;++k)hashes.push_back(b.u32(0x20+20*k+4*cut));
        }
        need(found,"start camera scene absent");
        Skeleton base;bool root_found=false;
        const auto common_path=root/"Global/INGAMEB.BUN";
        for(const auto& c:file_chunks(fs,common_path))if(c.id==0xe34009) {
            const auto at=(c.at+15)&~std::uint64_t(15);auto bytes=read_range(fs,common_path,at,std::size_t(c.end-at));Elf e{View(bytes)};
            const auto i=e.symbols.find("__Skeleton:::ROOT");if(i!=e.symbols.end()){need(!root_found,"duplicate camera ROOT");base=skeleton(e,i->second);root_found=true;}
        }
        need(root_found && base.pose.size()==2,"start camera ROOT shape");
        std::vector<Bytes> banks;std::vector<Elf> elves;std::map<unsigned,std::pair<std::size_t,std::size_t>> clips;
        std::size_t resident=0;
        for(const auto& c:parts)if(c.id==0xe34010) {
            const auto at=(c.at+15)&~std::uint64_t(15);auto bytes=read_range(fs,path,at,std::size_t(c.end-at));Elf e{View(bytes)};
            std::vector<std::pair<unsigned,std::size_t>> wanted;
            for(const auto& symbol:e.symbols)if(symbol.first=="__AnimationBank:::bank") {
                const auto n=e.data.u32(symbol.second+4),ptr=e.data.u32(symbol.second+12),names=e.data.u32(symbol.second+16);need(n<1024,"start camera bank size");
                for(unsigned i=0;i<n;++i) {
                    const auto h=hash(e.data.text(e.data.u32(names+4*i)));
                    if(std::find(hashes.begin(),hashes.end(),h)!=hashes.end())wanted.emplace_back(h,e.data.u32(ptr+4*i));
                }
            }
            if(wanted.empty())continue;
            resident+=bytes.size();need(resident<=16*1024*1024,"start camera banks budget");
            for(const auto& item:wanted)need(clips.emplace(item.first,std::make_pair(elves.size(),item.second)).second,"duplicate start camera clip");
            banks.push_back(std::move(bytes));elves.push_back(std::move(e));
        }
        for(std::size_t cut=0;cut<hashes.size();cut+=3) {
            unsigned frames=0;
            for(unsigned k=0;k<3;++k) {
                need(clips.count(hashes[cut+k]),"start camera clip absent");const auto a=clips.at(hashes[cut+k]);const auto& e=elves[a.first];
                const auto n=e.data.word(a.second+10);need(n && n<=1024 && (!frames||frames==n) && e.data.word(a.second+8)==1,"start camera clip domain");frames=n;
            }
            for(unsigned f=0;f<frames;++f) {
                auto pose=base.pose;
                for(unsigned k=0;k<3;++k){const auto a=clips.at(hashes[cut+k]);const auto& e=elves[a.first];channel(e.data,e.data.u32(a.second+12),f,pose);}
                next->camera_poses.push_back(pose[1]);
            }
            next->camera_cuts.push_back(next->camera_poses.size());
        }
        //47B290 flag20 holds the last sample when a clip ends before release.
        need(!next->camera_poses.empty(),"start camera has no samples");
        next->go_frame=timing.go;next->release_frame=timing.release;next->post_go_frames=timing.end-timing.go;
        data_=std::move(next);clock=timing;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool StartCrowd::load(FileSystemService& fs,const std::filesystem::path& root,std::string& error,unsigned scene,unsigned track_number,const std::string& requested_region) {
    try {
        const std::string region=track_number==1301 && requested_region=="L1RA"?"L1RB":requested_region;
        need(region.size()==4 && std::all_of(region.begin(),region.end(),[](unsigned char c){return (c>='A'&&c<='Z')||(c>='0'&&c<='9');}),"start crowd region");
        auto next=std::make_unique<Data>();
        auto track=read_crowd_file(fs,root/("Tracks/TRACKB"+std::to_string(track_number)+".LZC"),16*1024*1024);Bytes decoded;std::size_t unused=0;
        need(jdlz_decompress(track.data(),track.size(),16*1024*1024,decoded,unused,error),"crowd TRACKB decode");
        View tb(decoded);struct Record {Matrix local;std::uint32_t mesh,animation,skeleton;float speed;};std::vector<Record> records;
        for(auto c:chunks(tb,0,tb.size))if(c.id==0x3b300)for(auto a=align(c.at+8,16);a+160<=c.end;a+=160) {
            const bool starter=tb.u32(a)==0x59eeb6b1;
            if((tb.u32(a)!=0x4186bfa8 && !starter) || tb.u32(a+12)!=scene)continue;
            Record r{};for(unsigned k=0;k<16;++k)r.local[k]=tb.f32(a+16+4*k);
            r.mesh=tb.u32(a+80);r.animation=tb.u32(a+140);r.skeleton=tb.u32(a+144);r.speed=tb.f32(a+148);
            need(r.speed>0 && r.speed<=4,"crowd playback speed");
            // 5799AE: compound skeletal clips use half the 30Hz clock.
            if(starter)r.speed*=.5F;
            records.push_back(r);
        }
        need(!records.empty() && records.size()<=64,"start scene actors absent");
        Bytes().swap(track);Bytes().swap(decoded);
        auto common=read_crowd_file(fs,root/"Global/INGAMEB.BUN",32*1024*1024);View cb(common);std::map<std::uint32_t,Skeleton> skeletons;
        for(auto c:chunks(cb,0,cb.size))if(c.id==0xe34009) {
            auto at=align(c.at+8,16);Elf e(View(cb.p+at,c.end-at));
            for(const auto& s:e.symbols)if(s.first.find("__Skeleton:::")==0)skeletons.emplace(hash(s.first.substr(13)),skeleton(e,s.second));
        }
        Bytes().swap(common);
        // Retain only this scene's mesh/texture chunks and animation banks.
        // Copy the original header alignment so ELF/geometry offsets remain
        // unchanged, without keeping the entire city stream in the heap.
        const auto stream_path=root/("Tracks/STREAM"+region+".BUN");const auto parts=file_chunks(fs,stream_path);
        Bytes stream;std::vector<std::uint32_t> crowd_textures;
        const auto append=[&](std::uint64_t at,const Bytes& bytes) {
            const auto delta=std::size_t((at-stream.size())&127);
            if(delta) {const auto n=delta>=8?delta:delta+128;const auto p=stream.size();stream.resize(p+n,0);const auto payload=std::uint32_t(n-8);std::memcpy(stream.data()+p+4,&payload,4);}
            need(stream.size()+bytes.size()<=32*1024*1024,"selected crowd stream budget");
            stream.insert(stream.end(),bytes.begin(),bytes.end());
        };
        for(const auto& c:parts)if(c.id==0x80134010) {
            bool wanted=false;
            for(const auto& h:parts)if(h.id==0x134011 && h.at>=c.at && h.end<=c.end) {
                wanted=std::any_of(records.begin(),records.end(),[&](const Record& r){return r.mesh==h.mesh_hash;});break;
            }
            if(!wanted)continue;
            const auto header=c.at-8;auto bytes=read_range(fs,stream_path,header,std::size_t(c.end-header));
            // A leading empty chunk preserves the original 128-byte alignment.
            auto prefix=std::size_t(header&127);if(prefix && prefix<8)prefix+=128;Bytes probe(prefix+bytes.size(),0);
            if(prefix){const auto n=std::uint32_t(prefix-8);need(prefix>=8,"crowd mesh alignment");std::memcpy(probe.data()+4,&n,4);}
            std::copy(bytes.begin(),bytes.end(),probe.begin()+prefix);
            const auto m=mesh(View(probe),{c.id,prefix,probe.size()});
            crowd_textures.insert(crowd_textures.end(),m.textures.begin(),m.textures.end());append(header,bytes);
        }
        for(const auto& c:parts)if(c.id==0xe34010 || c.id==0x34035) {
            const auto header=c.at-8;auto bytes=read_range(fs,stream_path,header,std::size_t(c.end-header));
            if(c.id==0x34035 && (bytes.size()<16 || View(bytes).u32(12)!=scene))continue;
            append(header,bytes);
        }
        for(const auto& c:parts)if(c.id==kTexturePackRootId) {
            bool indexed=false,wanted=false;
            auto table=std::upper_bound(parts.begin(),parts.end(),c.at,
                [](auto at,const FileChunk& item){return at<item.at;});
            for(;table!=parts.end()&&table->at<c.end;++table)if(table->id==kTextureInfoTableId) {
                const auto bytes=table->end-table->at;
                if(!bytes || bytes%kTextureInfoRecordSize){indexed=false;break;}
                indexed=true;
                const auto info=read_range(fs,stream_path,table->at,std::size_t(bytes));
                for(std::size_t i=0;i<info.size();i+=kTextureInfoRecordSize) {
                    const auto hash=View(info).u32(i+36);
                    if(std::find(crowd_textures.begin(),crowd_textures.end(),hash)!=crowd_textures.end()) {
                        wanted=true;break;
                    }
                }
                if(wanted)break;
            }
            if(indexed && !wanted)continue;
            const auto header=c.at-8;auto bytes=read_range(fs,stream_path,header,std::size_t(c.end-header));
            const auto packs=parse_texture_pack_set(bytes.data(),bytes.size());
            for(const auto& p:packs.packs)for(const auto& t:p.textures)
                wanted|=std::find(crowd_textures.begin(),crowd_textures.end(),t.hash)!=crowd_textures.end();
            if(wanted)append(header,bytes);
        }
        View sb(stream);
        std::map<std::uint32_t,Chunk> meshes;std::vector<std::pair<Elf,std::size_t>> animations;std::map<std::uint32_t,std::size_t> anim_index;
        bool scene_timing_found=false;
        std::vector<std::uint32_t> camera_hashes;
        for(auto c:chunks(sb,0,sb.size,true))if(c.id==0x34035 && c.end-c.at>=0x84 && sb.u32(c.at+12)==scene) {
            const auto a=c.at+8;
            if(sb.u32(a+0x6c)) {
                const auto count=sb.u32(a+8);need(count && count<=5,"drift start camera count");
                for(unsigned cut=0;cut<count;++cut)for(unsigned k=0;k<3;++k)camera_hashes.push_back(sb.u32(a+0x20+20*k+4*cut));
            }
        }
        for(auto c:chunks(sb,0,sb.size,true)) {
            // 47B890 binds 34035 payload directly; 47F0AC reads end (+64)
            // and countdown end (+68). Scene 2338 is the UG0 DDay start.
            if(c.id==0x34035 && c.end-c.at>=0x84 && sb.u32(c.at+12)==scene) {
                need(!scene_timing_found,"duplicate crowd scene timing");
                const auto end=sb.u32(c.at+8+0x64),go=sb.u32(c.at+8+0x68);
                need(end>=go && end<=0x7fffffffU,"invalid crowd scene timing");
                next->go_frame=go;next->post_go_frames=end-go;next->release_frame=sb.u32(c.at+8+0x6c);scene_timing_found=true;
            }
            if(c.id==0x80134010) {
                for(auto h:chunks(sb,c.at+8,c.end))if(h.id==0x134011) {
                    const auto at=align(h.at+8,16);const auto id=sb.u32(at+16);
                    if(std::any_of(records.begin(),records.end(),[&](const Record& r){return r.mesh==id;}))meshes.emplace(id,c);
                }
            }
            if(c.id==0xe34010) {
                const auto at=align(c.at+8,16);Elf e(View(sb.p+at,c.end-at));
                for(const auto& s:e.symbols)if(s.first=="__AnimationBank:::bank") {
                    const auto n=e.data.u32(s.second+4),ptr=e.data.u32(s.second+12),names=e.data.u32(s.second+16);need(n<1024,"crowd bank size");
                    for(unsigned i=0;i<n;++i) {
                        auto name=e.data.text(e.data.u32(names+4*i));auto h=hash(name);
                        if(!anim_index.count(h) && (std::find(camera_hashes.begin(),camera_hashes.end(),h)!=camera_hashes.end() || std::any_of(records.begin(),records.end(),[&](const Record& r){return r.animation==h;}))) {
                            anim_index[h]=animations.size();animations.emplace_back(e,e.data.u32(ptr+4*i));
                        }
                    }
                }
            }
        }
        need(scene_timing_found,"crowd scene timing absent");
        if(scene==kFirstDriftStartScene) {
            next->car_poses=drift_car_motion(root,region);
            next->car_travel.assign(next->car_poses.size(),0);
            for(std::size_t i=1;i<next->car_poses.size();++i) {
                const auto a=pose_matrix(next->car_poses[i-1]),b=pose_matrix(next->car_poses[i]);
                double distance=0;for(unsigned k=0;k<3;++k)distance+=double(b[12+k]-a[12+k])*a[k];
                next->car_travel[i]=next->car_travel[i-1]+distance;
            }
        }
        if(next->release_frame) {
            need(skeletons.count(hash("ROOT")),"start camera ROOT skeleton absent");
            const auto& skeleton=skeletons.at(hash("ROOT"));need(skeleton.pose.size()==2,"camera skeleton shape");
            for(std::size_t cut=0;cut<camera_hashes.size();cut+=3) {
            unsigned frames=0;
            for(auto h:{camera_hashes[cut],camera_hashes[cut+1],camera_hashes[cut+2]}) {
                need(anim_index.count(h),"start camera animation absent");
                const auto& a=animations.at(anim_index.at(h));
                const auto n=a.first.data.word(a.second+10);
                need(n && n<=1024 && (!frames||frames==n),"camera animation frame count");frames=n;
                need(a.first.data.word(a.second+8)==1,"camera compound channel count");
            }
            next->camera_poses.reserve(frames);
            for(unsigned f=0;f<frames;++f) {
                auto pose=skeleton.pose;
                for(auto h:{camera_hashes[cut],camera_hashes[cut+1],camera_hashes[cut+2]}) {
                    const auto& a=animations.at(anim_index.at(h));
                    channel(a.first.data,a.first.data.u32(a.second+12),f,pose);
                }
                next->camera_poses.push_back(pose[1]);
            }
            next->camera_cuts.push_back(next->camera_poses.size());
            }
        }
        std::map<std::tuple<std::uint32_t,std::uint32_t,float>,std::size_t> clips;std::vector<std::uint32_t> wanted;
        for(const auto& r:records) {
            need(meshes.count(r.mesh) && anim_index.count(r.animation) && skeletons.count(r.skeleton),"crowd actor resource missing");
            auto key=std::make_tuple(r.mesh,r.animation,r.speed);auto found=clips.find(key);std::size_t index;
            if(found==clips.end()) {
                index=next->clips.size();clips[key]=index;Data::Clip c;c.mesh=mesh(sb,meshes.at(r.mesh));
                const auto& anim=animations.at(anim_index.at(r.animation));const auto frames=anim.first.data.word(anim.second+10);
                need(frames && frames<=1024,"crowd clip frame count");
                c.skeleton=skeletons.at(r.skeleton);c.speed=r.speed;c.frames.reserve(frames);
                for(unsigned f=0;f<frames;++f) {
                    auto pose=c.skeleton.pose;const auto count=anim.first.data.word(anim.second+8);
                    need(count==3,"crowd compound channel count");
                    for(unsigned k=0;k<count;++k)channel(anim.first.data,anim.first.data.u32(anim.second+12+4*k),f,pose);
                    c.frames.push_back(std::move(pose));
                }
                c.pose=c.frames.front();c.world.resize(c.pose.size());c.skin.resize(c.pose.size());
                c.vertices.reserve(c.mesh.vertices.size());skin_pose(c.pose,c.skeleton,c.world,c.skin);
                skin_mesh(c.mesh,c.skin,c.vertices);
                for(auto h:c.mesh.textures)wanted.push_back(h);
                next->clips.push_back(std::move(c));
            }else index=found->second;
            next->actors.push_back({r.local,index,0,0,r.speed});
        }
        std::sort(wanted.begin(),wanted.end());wanted.erase(std::unique(wanted.begin(),wanted.end()),wanted.end());
        const auto packs=parse_texture_pack_set(stream.data(),stream.size());RenderTextureSource source;source.bytes=&stream;source.wanted=&wanted;
        for(const auto& p:packs.packs)source.packs.push_back(&p);
        RenderTextureLimits limits;limits.max_total_bytes=8*1024*1024;
        need(decode_render_texture_sources({source},limits,next->textures,error),"crowd texture decode failed");
        auto& table=next->commands.owning_uploads();std::map<std::uint32_t,std::size_t> texture_resources;
        for(auto& t:next->textures.textures)if(t.valid) {
            UploadResource r;r.name="start-crowd/texture";r.kind=UploadResourceKind::texture;r.memory=UploadMemory::image;r.data=t.rgba.data();r.bytes=t.rgba.size();r.alignment=kTextureStagingAlignment;r.element_bytes=4;r.elements=std::uint64_t(t.width)*t.height;r.texture_hash=t.hash;
            need(check_texture_upload_format(TextureUploadFormat::rgba8,t.width,t.height,r.bytes,r.texture,error),"crowd texture shape");
            texture_resources[t.hash]=table.resources.size();table.resources.push_back(r);++table.totals.textures;table.totals.texture_bytes+=r.bytes;table.totals.largest_texture_bytes=std::max(table.totals.largest_texture_bytes,r.bytes);
        }
        // Stream only the current interpolated skin through the backend's
        // existing fenced frame slots. The CPU pointers stay stable; no
        // duplicate whole-mesh poses or per-frame allocation is required.
        for(auto& c:next->clips) {
            c.vertex=table.resources.size();UploadResource r;r.name="crowd/current-pose";
            r.kind=UploadResourceKind::vertex_buffer;r.memory=UploadMemory::buffer;r.streamed_vertices=true;
            r.data=reinterpret_cast<const unsigned char*>(c.vertices.data());r.bytes=c.vertices.size()*sizeof(MeshVertex);
            r.element_bytes=sizeof(MeshVertex);r.elements=c.vertices.size();r.alignment=alignof(MeshVertex);
            table.resources.push_back(r);++table.totals.vertex_buffers;table.totals.vertex_bytes+=r.bytes;
            UploadResource idx;idx.name="crowd/indices";idx.kind=UploadResourceKind::index_buffer;idx.memory=UploadMemory::buffer;
            idx.data=reinterpret_cast<const unsigned char*>(c.mesh.indices.data());idx.bytes=c.mesh.indices.size()*2;
            idx.element_bytes=2;idx.elements=c.mesh.indices.size();idx.alignment=2;
            c.index=table.resources.size();table.resources.push_back(idx);
            ++table.totals.index_buffers;table.totals.index_bytes+=idx.bytes;
        }
        table.totals.resources=table.resources.size();next->resident=table.totals.vertex_bytes+table.totals.index_bytes+table.totals.texture_bytes;
        for(const auto& c:next->clips) {
            next->resident+=c.mesh.vertices.size()*sizeof(SkinVertex)+c.pose.size()*sizeof(Pose);
            next->resident+=(c.world.size()+c.skin.size()+c.skeleton.inverse.size())*sizeof(Matrix);
            next->resident+=c.skeleton.pose.size()*sizeof(Pose)+c.skeleton.parent.size()*sizeof(int);
            for(const auto& frame:c.frames)next->resident+=frame.size()*sizeof(Pose);
        }
        need(next->resident<48*1024*1024,"crowd animation payload exceeds budget");
        for(auto& a:next->actors) {
            const auto& c=next->clips[a.clip];a.first_draw=next->commands.owning_draws().size();
            for(const auto& m:c.mesh.materials) {
                RecordedDraw d;d.name="start-crowd/actor";d.vertex_resource=c.vertex;d.index_resource=c.index;
                const auto h=c.mesh.textures[m.texture_slot];need(texture_resources.count(h)!=0,"crowd texture unresolved");d.texture_resource=texture_resources.at(h);
                d.vertex_count=std::uint32_t(c.vertices.size());d.first_index=m.first_index;d.index_count=m.index_count;
                for(const auto& p:shader_manifest::kPrograms)if(std::strcmp(p.name,"car_textured")==0) {d.program=&p;for(std::size_t i=0;i<p.variant_count;++i)if(std::strcmp(p.variants[i].name,"opaque")==0)d.pipeline=p.variants[i];}
                need(d.program && d.pipeline.name,"crowd shader absent");d.pipeline.cull=shader_manifest::Cull::none;d.material.fallback[3]=2;
                next->commands.owning_draws().push_back(d);++a.draws;
            }
        }
        data_=std::move(next);error.clear();return true;
    }catch(const std::exception& e) {error=e.what();return false;}
}
RaceStartClock StartCrowd::clock() const {return {data_->release_frame,data_->go_frame,data_->go_frame+data_->post_go_frames};}
void StartCrowd::frame(const RaceSession& session) {
    const auto& timing=session.timing();
    // 47F4C0 completes at the scene end; 423611 then destroys the NIS
    // controller and its crowd. GO alone does not retire these actors.
    // Both clocks include the authored cinematic when enabled.
    const auto elapsed=std::int64_t(timing.now_ticks)-timing.go_ticks;
    const bool ended=timing.go_valid && elapsed*30>=
        std::int64_t(data_->post_go_frames)*kRaceTicksPerSecond;
    data_->active=!ended && timing.outcome==RaceOutcome::running?
        data_->commands.owning_draws().size():0;
    if(!data_->active && data_->camera_poses.empty())return;
    if(!data_->anchored) {
        // 47CAC0 selects ForwardStartLine(0), not the player's grid slot.
        // 47CB50 aligns its plane with the road and adds the scene rotation.
        data_->anchor=start_anchor(session.course());
        data_->anchored=true;
    }
    const double seconds=double(session.start_scene_ticks())/kRaceTicksPerSecond;
    if(data_->active)for(auto& c:data_->clips) {
        const double phase=std::fmod(std::max(0.0,seconds)*30*c.speed,double(c.frames.size()));
        if(phase==c.sampled)continue;
        const auto first=std::size_t(phase),second=std::min(first+1,c.frames.size()-1);
        const float fraction=float(phase-double(first));
        for(std::size_t i=0;i<c.pose.size();++i)
            need(eagl::blend_pose(c.frames[first][i],c.frames[second][i],fraction,c.pose[i]),"crowd interpolated quaternion invalid");
        skin_pose(c.pose,c.skeleton,c.world,c.skin);skin_mesh(c.mesh,c.skin,c.vertices);c.sampled=phase;
    }
    for(const auto& a:data_->actors) {
        const auto world=multiply(a.local,data_->anchor);
        for(std::size_t i=0;i<a.draws;++i)std::copy(world.begin(),world.end(),data_->commands.owning_draws()[a.first_draw+i].placement.rows);
    }
}
bool load_drift_start_pose(const std::filesystem::path& root,const VehicleTrackCourse& course,VehiclePose& out,std::string& error,const std::string& region) {
    try {
        const auto motion=drift_car_motion(root,region);const auto m=multiply(pose_matrix(motion.back()),start_anchor(course));
        TrackContact contact;const float point[3]={m[12],m[13],m[14]};
        need(query_track_contact_point(course.world,point,2,8,contact)==TrackContactStatus::hit,"drift car final road absent");
        out={};out.ground={m[12],m[13],contact.point[2]};out.heading=std::atan2(m[1],m[0]);
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool StartCrowd::car(const RaceSession& session,VehicleState& out,CarAnimationState* animation) const {
    if(!data_->anchored || data_->car_poses.empty())return false;
    // 47C400 speed=.5: native 47B290 multiplies the 30Hz clock by speed.
    const double frame=std::max(0.0,double(session.start_scene_ticks())*15/kRaceTicksPerSecond);
    const auto a=std::min(std::size_t(frame),data_->car_poses.size()-1),b=std::min(a+1,data_->car_poses.size()-1);
    Pose pose{};const float f=float(frame-std::floor(frame));
    for(unsigned k=0;k<12;++k)pose[k]=data_->car_poses[a][k]+f*(data_->car_poses[b][k]-data_->car_poses[a][k]);
    const auto m=multiply(pose_matrix(pose),data_->anchor),last=multiply(pose_matrix(data_->car_poses.back()),data_->anchor);
    const double heading=std::atan2(m[1],m[0]);
    if(animation) {
        const double travel=data_->car_travel[a]+double(f)*(data_->car_travel[b]-data_->car_travel[a]);
        for(unsigned w=0;w<kVehicleWheelCount;++w) {
            const double radius=session.spec().handling.wheels[w].radius;
            if(radius>0)animation->wheel_phase[w]=std::remainder(session.animation().wheel_phase[w]+travel/radius,6.28318530717958647692);
        }
    }
    if(!session.start_cinematic())return false;
    out=session.car();out.position.x+=m[12]-last[12];out.position.y+=m[13]-last[13];
    out.orientation={std::cos(heading*.5),0,0,std::sin(heading*.5)};
    return true;
}
bool StartCrowd::camera(const RaceSession& session,OrbitCamera& out,unsigned width,unsigned height) const {
    if(!data_->anchored || data_->camera_poses.empty() || !session.start_cinematic())return false;
    const double frame=std::max(0.0,double(session.start_scene_ticks())*30/kRaceTicksPerSecond);
    const auto a=std::min(std::size_t(frame),data_->camera_poses.size()-1);
    auto b=std::min(a+1,data_->camera_poses.size()-1);
    if(std::find(data_->camera_cuts.begin(),data_->camera_cuts.end(),b)!=data_->camera_cuts.end())b=a;
    const float fraction=float(frame-std::floor(frame));Pose pose{};
    for(unsigned k=0;k<12;++k)pose[k]=data_->camera_poses[a][k]+fraction*(data_->camera_poses[b][k]-data_->camera_poses[a][k]);
    const auto focal=pose[0];pose[0]=pose[1]=pose[2]=pose[3]=1;
    const auto m=multiply(pose_matrix(pose),data_->anchor);
    // Native 48E490 uses ROOT0 matrix: eye=row3, target=eye-row2,
    // up=row1. World (x,y,z) becomes renderer (x,z,-y).
    const Vec3 eye{m[12],m[14],-m[13]},back{m[8],m[10],-m[9]};
    const double length=std::sqrt(back.x*back.x+back.y*back.y+back.z*back.z);
    if(!(length>0) || !(focal>0))return false;
    out.target={eye.x-back.x,eye.y-back.y,eye.z-back.z};out.distance=length;
    constexpr double degrees=180/3.14159265358979323846;
    out.yaw_deg=std::atan2(back.z,back.x)*degrees;
    out.pitch_deg=std::asin(std::clamp(back.y/length,-1.0,1.0))*degrees;
    // 48E62F converts the authored lens to the primary view's angle word.
    // Rebuild40DD00 projection too: retaining chase focal overrides ignored
    // every authored change of lens even though fov_y_deg was updated.
    ChaseCameraView lens; lens.fov=std::uint16_t(2U*player_velocity_angle(2*focal,36.0099983215332F));
    lens.near_plane=out.near_plane;lens.far_plane=out.far_plane;
    OrbitCamera projection;std::string error;
    if(!drive_render_camera(lens,ChaseFovMode::retail,width,height,projection,error))return false;
    out.fov_y_deg=projection.fov_y_deg;
    out.focal_x_override=projection.focal_x_override;out.focal_y_override=projection.focal_y_override;
    const double yaw=out.yaw_deg/degrees,pitch=out.pitch_deg/degrees;
    const Vec3 right{std::sin(yaw),0,-std::cos(yaw)};
    const Vec3 up{-std::sin(pitch)*std::cos(yaw),std::cos(pitch),-std::sin(pitch)*std::sin(yaw)};
    const Vec3 authored_up{m[4],m[6],-m[5]};
    out.roll_deg=std::atan2(authored_up.x*right.x+authored_up.y*right.y+authored_up.z*right.z,
                          authored_up.x*up.x+authored_up.y*up.y+authored_up.z*up.z)*degrees;
    return true;
}
bool StartCrowd::render(const RenderOptions& options,Frame& target,RenderStats& stats,std::string& error)const {
    CameraFrame camera;if(!make_camera_frame(options.camera,target.width(),target.height(),camera,error))return false;
    Rasterizer raster(target,options,{},stats);const auto& uploads=data_->commands.resource_table();
    for(std::size_t i=0;i<data_->active;++i) {
        const auto& d=data_->commands.owning_draws()[i];const auto& vr=uploads.resources.at(d.vertex_resource-data_->upload_base);
        const auto& ir=uploads.resources.at(d.index_resource-data_->upload_base);const auto& tr=uploads.resources.at(d.texture_resource-data_->upload_base);
        const auto* vertices=reinterpret_cast<const MeshVertex*>(vr.data);const auto* indices=reinterpret_cast<const std::uint16_t*>(ir.data);
        RasterMaterial material;material.linear_filter=true;
        for(const auto& t:data_->textures.textures)if(t.hash==tr.texture_hash) {material.texture=&t;break;}
        if(!material.texture) {error="crowd CPU texture missing";return false;}
        for(unsigned j=d.first_index;j<d.first_index+d.index_count;j+=3) {
            ClipVertex clip[3];for(unsigned k=0;k<3;++k) {const auto& v=vertices[d.first_vertex+indices[j+k]];clip[k]=project_point(camera,placed_vertex(v,d.placement.rows));clip[k].u=v.u;clip[k].v=v.v;}
            if(!raster.draw_triangle(clip,material,std::uint32_t(stats.triangles_submitted++),error))return false;
        }
    }return true;
}
}
