#include "nfsu/minimap.h"
#include "nfsu/arg_path.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace nfsu;
namespace {
int failures=0;
void check(bool ok,const char* name) { if(!ok) { ++failures; std::cerr<<"FAIL: "<<name<<'\n'; } }
MinimapResource fixture() {
    MinimapResource r; r.valid=true; r.hash=0xA4F2F595; r.width=r.height=256;
    r.offset_x=604.030029296875F; r.offset_y=796.5999755859375F; r.world_span=1202;
    return r;
}
MinimapStyle style3() {
    MinimapStyle s; s.index=3; s.tilt=65535; s.distance=200;
    s.extent_add={{30,30}}; s.uv_extent={{0.33000001311302185F,0.33000001311302185F}};
    s.route_alpha=0.20000000298023224F; s.centered=s.heading_aware=s.enabled=true; return s;
}
MinimapPresentation presentation() {
    MinimapPresentation p; p.valid=p.camera_angle_valid=true; p.map_angle=0x8000;
    p.native_center={{86,240}}; p.native_half_extent={{64,64}};
    p.screen_transform={{1,0,0,0,1,0}}; return p;
}
bool cleared(const MinimapGeometry& out) {
    if(out.count || out.center!=MapPoint{}) return false;
    for(auto xy:out.boundary) if(xy!=MapPoint{}) return false;
    for(const auto& piece:out.pieces) {
        if(piece.actor_id || piece.kind || piece.texture_hash || piece.route) return false;
        for(const auto& v:piece.vertices) if(v.screen!=MapPoint{} ||
            v.uvq!=std::array<float,3>{} || v.color!=std::array<std::uint8_t,4>{}) return false;
    }
    return true;
}
void guards() {
    auto r=fixture(); auto s=style3(); auto p=presentation(); MinimapGeometry out;
    MapPoint uv; check(project_minimap_world(r,{{1646.3746337890625F,268.5751647949219F}},uv),"project finite");
    check(uv[0]==0.8671801090240479F && uv[1]==0.4392877519130707F,"native node11 float pin");
    check(minimap_scalar_sine(0x4000)==1.0000035762786865F,"retail cosine not identity");
    MinimapActor a; a.id=100; a.raw_xy={{1646.3746337890625F,268.5751647949219F}};
    a.raw_gates_valid=true; a.gate_4d0=a.gate_8a9=1; a.size_x=a.size_y=0.1F;
    a.color={{12,34,56}}; a.kind=5; a.texture_hash=0x7E22DED8;
    const auto run=[&](std::size_t capacity=kMinimapMaxPieces) {
        return build_minimap_geometry(r,s,p,a.raw_xy,&a,1,out,capacity);
    };
    check(run()==MinimapResult::ok && out.count>1,"bounded route and marker");
    const auto saved=out; check(run(0)==MinimapResult::capacity_exceeded && out.count==0,"overflow atomic");
    a.raw_gates_valid=false; check(run()==MinimapResult::unknown_input && out.count==0,"unknown raw gates");
    a.raw_gates_valid=true; a.gate_4d0=0; check(run()==MinimapResult::ok && out.count<saved.count,"raw gate removal");
    a.gate_4d0=1; p.camera_angle_valid=false; check(run()==MinimapResult::unknown_input,"unknown camera");
    p.camera_angle_valid=true; p.valid=false; check(run()==MinimapResult::unknown_input,"unknown final transform");
    p.valid=true; s.index=11; check(run()==MinimapResult::unknown_input,"unsupported style11");
    s=style3(); s.distance=0; check(run()==MinimapResult::invalid_input,"zero distance");
    s=style3(); s.tilt=0; s.distance=std::numeric_limits<float>::denorm_min(); out=saved;
    check(run()==MinimapResult::invalid_input && cleared(out),"tiny distance UVQ rejection atomic");
    s=style3(); s.uv_extent={{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()}}; out=saved;
    check(run()==MinimapResult::invalid_input && cleared(out),"extreme finite UV extent rejection atomic");
    s=style3(); p.native_center={{std::numeric_limits<float>::max(),0}};
    p.screen_transform={{1000000,0,0,0,1,0}}; out=saved;
    check(run()==MinimapResult::invalid_input && cleared(out),"final transform overflow rejection atomic");
    p=presentation(); s=style3(); s.tilt=0;
    p.native_center[0]=std::numeric_limits<float>::max()*0.75F;
    p.native_half_extent[0]=std::numeric_limits<float>::max()*0.5F;
    s.extent_add[0]=0; s.offset[0]=-p.native_center[0]; out=saved;
    check(run()==MinimapResult::invalid_input && cleared(out),"clip bound overflow rejection atomic");
    p=presentation();
    s=style3(); a.raw_xy[0]=std::numeric_limits<float>::quiet_NaN(); check(run()==MinimapResult::invalid_input,"nonfinite pose");
    a.raw_xy={{1646.3746337890625F,268.5751647949219F}};
    std::array<MinimapActor,2> duplicate{{a,a}};
    check(build_minimap_geometry(r,s,p,a.raw_xy,duplicate.data(),2,out)==MinimapResult::invalid_input,"duplicate IDs");
    check(build_minimap_geometry(r,s,p,a.raw_xy,&a,kMinimapMaxActors+1,out)==MinimapResult::capacity_exceeded,"actor capacity before dereference");
    s.index=6; s.enabled=false; check(run()==MinimapResult::ok && out.count==0,"disabled style6");
    r.valid=false; check(run()==MinimapResult::invalid_resource,"invalid resource");
}
void oracle(const MinimapResource& r) {
    std::cout.precision(9); char command;
    while(std::cin>>command) {
        if(command=='P') {
            MapPoint xy,uv; std::cin>>xy[0]>>xy[1];
            if(!project_minimap_world(r,xy,uv)) { check(false,"oracle projection"); return; }
            std::cout<<"P "<<uv[0]<<' '<<uv[1]<<'\n';
        } else if(command=='G') {
            auto p=presentation(); MinimapStyle s; unsigned tilt,map_angle,centered,heading,enabled;
            MapPoint view; std::size_t count;
            std::cin>>s.index>>tilt>>s.distance>>s.offset[0]>>s.offset[1]>>s.extent_add[0]>>s.extent_add[1]
                >>s.uv_extent[0]>>s.uv_extent[1]>>s.route_alpha>>centered>>heading>>enabled>>map_angle>>view[0]>>view[1]>>count;
            s.tilt=std::uint16_t(tilt); s.centered=centered!=0; s.heading_aware=heading!=0; s.enabled=enabled!=0;
            p.map_angle=std::uint16_t(map_angle);
            std::array<MinimapActor,kMinimapMaxActors> actors;
            if(count>actors.size()) { check(false,"oracle actor count"); return; }
            for(std::size_t i=0;i<count;++i) {
                auto& a=actors[i]; unsigned h,c0,c1,c2,g0,g1;
                std::cin>>a.id>>a.raw_xy[0]>>a.raw_xy[1]>>h>>a.size_x>>a.size_y>>c0>>c1>>c2>>a.kind>>g0>>g1>>a.texture_hash;
                a.heading=std::uint16_t(h); a.color={{std::uint8_t(c0),std::uint8_t(c1),std::uint8_t(c2)}};
                a.raw_gates_valid=true; a.gate_4d0=std::uint8_t(g0); a.gate_8a9=std::uint8_t(g1);
            }
            MinimapGeometry out; const auto result=build_minimap_geometry(r,s,p,view,actors.data(),count,out);
            if(!std::cin || result!=MinimapResult::ok) { check(false,"oracle geometry"); return; }
            std::cout<<"G "<<out.count<<' '<<out.center[0]<<' '<<out.center[1];
            for(auto v:out.boundary) std::cout<<' '<<v[0]<<' '<<v[1];
            std::cout<<'\n';
            for(std::size_t i=0;i<out.count;++i) {
                const auto& q=out.pieces[i]; std::cout<<"Q "<<q.route<<' '<<q.actor_id<<' '<<q.kind;
                for(const auto& v:q.vertices) {
                    for(auto t:v.screen) std::cout<<' '<<t;
                    for(auto t:v.uvq) std::cout<<' '<<t;
                    for(auto t:v.color) std::cout<<' '<<unsigned(t);
                }
                std::cout<<'\n';
            }
        } else { check(false,"oracle unknown command"); return; }
    }
}
}
int main(int argc,char** argv) {
    std::string error; if(!check_arguments(argc,argv,1,error)) { std::cerr<<error<<'\n'; return 1; }
    if(argc>=3 && std::string(argv[1])=="--retail") {
        MinimapResource r; check(load_minimap_resource(argument_path(argv[2]),r,error),"retail descriptor");
        if(!r.valid) { std::cerr<<error<<'\n'; return 1; }
        check(r.payload_offset==819968 && r.info_offset==840 && r.payload_bytes==262144,"retail resource metadata");
        check(r.offset_x==604.030029296875F && r.offset_y==796.5999755859375F && r.world_span==1202 && r.rotation==0,"retail track metadata");
        if(argc==4 && std::string(argv[3])=="--oracle") oracle(r);
        else guards();
    } else guards();
    if(failures) { std::cerr<<"FAIL: "<<failures<<" minimap checks\n"; return 1; }
    std::cout<<"PASS: minimap checks\n"; return 0;
}
