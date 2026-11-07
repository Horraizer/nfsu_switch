#include "nfsu/race_skid.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/arg_path.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace nfsu;
namespace {
#include "wheel_skid_usage_native.inc"
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
void near(double a,double b){check(std::abs(a-b)<1.e-6,"native587E90 output differs");}
}
int main(int argc,char** argv){try {
    // Genuine original587E90 inputs/outputs with identity wheel basis and
    // explicit flat world-height query boundary. See tools/wheel_skid_native.py.
    struct Case {float vx,vy,slip,load;bool sliding,contact;unsigned material;bool emits;float cx,cy,alpha;};
    const Case cases[]={
        {20,0,0,3,false,true,1,false,0,0,0},
        {20,5,0,3,false,true,1,true,-.024705879390239716F,.09882351756095886F,.32338082790374756F},
        {20,0,4,3,false,true,1,true,0,.10499998927116394F,.3333333432674408F},
        {20,0,0,3,true,true,1,true,0,.10499998927116394F,1},
        {0,0,10,3,true,true,1,false,0,0,0},
        {20,5,0,3,true,false,1,false,0,0,0},
        {20,5,0,3,true,true,13,false,0,0,0},
        {1,5,0,3,true,true,1,true,-.051480479538440704F,.010296096093952656F,.30000001192092896F}
    };
    for(const auto& c:cases) {
        WheelSkidInput in;in.point={10,20,0};in.width=.21F;in.contact=c.contact;in.sliding=c.sliding;
        in.velocity={c.vx,c.vy,0};in.friction_usage=std::hypot(c.slip,c.vy);in.load_kn=c.load;in.surface=c.material;
        WheelSkidSample out;check(original_wheel_skid(in,out)==c.emits,"native587E90 emission gate differs");
        if(c.emits){near(out.cross_section.x,c.cx);near(out.cross_section.y,c.cy);near(out.opacity,c.alpha);near(out.point.x,10);near(out.point.y,20);}
    }
    const auto real=[](unsigned bits){float value;std::memcpy(&value,&bits,4);return value;};
    for(const auto& c:kNativeWheelSkidUsage) {
        PlayerWheelWork wheel;wheel.force_long=real(c[0]);wheel.force_lat=real(c[1]);
        wheel.load=real(c[2]);wheel.torque=real(c[3]);wheel.sliding=c[4]!=0;wheel.locked=c[5]!=0;wheel.contact=c[6]!=0;
        check(player_surface_factors(1,wheel.surface),"original pavement material");
        player_wheel_limit(stock_240sx_handling(),0,wheel);
        near(wheel.friction_usage,real(c[7]));
    }
    WheelSkidInput rolling;rolling.width=.21F;rolling.contact=true;rolling.velocity={40,5,0};rolling.surface=1;
    rolling.friction_usage=1;rolling.load_kn=3;
    WheelSkidSample ordinary;
    check(!original_wheel_skid(rolling,ordinary),"normal loaded tyre does not draw just because world lateral speed is high");
    rolling.sliding=true;check(original_wheel_skid(rolling,ordinary),"actual sliding still emits at normal wheel load");
    WheelSkidInput in;in.width=.21F;in.contact=true;in.sliding=true;in.velocity={20,0,0};in.surface=1;
    WheelSkidSample s;check(original_wheel_skid(in,s),"sliding road emits");
    auto marks=std::make_unique<RaceSkidMarks>();marks->observe(0,&s);s.point.x=1;marks->observe(0,&s);
    check(marks->segment_count()==1,"two valid wheel samples form a ribbon");
    marks->observe(0,nullptr);s.point.x=10;marks->observe(0,&s);check(marks->segment_count()==1,"air gap does not bridge marks");
    s.point.x=100;marks->observe(0,&s);check(marks->segment_count()==1,"teleport does not bridge marks");
    for(unsigned i=0;i<2000;++i){s.point.x+=3.1;marks->observe(0,&s);}
    check(marks->segment_count()==RaceSkidMarks::kSegments,"mark pool is bounded");
    in.inhibited=true;check(!original_wheel_skid(in,s),"wheel inhibit breaks ribbon");in.inhibited=false;
    in.surface=8;check(original_wheel_skid(in,s)&&s.texture==1,"dirt selects original dirt texture");near(s.cross_section.y,.147);
    in.surface=15;check(original_wheel_skid(in,s)&&s.texture==2,"grass selects original grass texture");
    in.surface=14;check(!original_wheel_skid(in,s),"surface without skid texture stays clear");
    if(argc==2) {
        const auto root=argument_path(argv[1]);std::string error;CarAssets car;
        check(load_car_assets(root/"Global/GLOBALB.BUN","CIVIC",car,error),error.c_str());
        check(marks->load(root,car,error),error.c_str());
        in.surface=1;in.point={10,20,3};check(original_wheel_skid(in,s),"real skid sample");
        marks->observe(0,&s);s.point.x=11;marks->observe(0,&s);
        RecordedCommands commands;RenderOptions options;check(record_frame_state(options,{},commands,error),error.c_str());
        check(marks->append(commands,error),error.c_str());check(validate_recorded_commands(commands,error),error.c_str());
        check(commands.draw_count()==3,"one batch per native material");
        marks->place(commands,0);
        check(commands.owning_draws()[0].material.alpha[0]==1&&commands.owning_draws()[1].material.alpha[0]==0,
            "only material containing live marks contributes pixels");
        const auto& r=commands.resource_table().resources.front();
        const auto* v=reinterpret_cast<const MeshVertex*>(r.data);
        near(v[0].px,10);near(v[0].py,3.05);near(v[0].pz,19.895);
        check(v[0].color[0]==128&&v[0].color[3]==127,"original constructor RGB and half opacity retained");
        const auto* stable=r.data;s.point.x=12;marks->observe(0,&s);marks->place(commands,0);
        check(r.data==stable,"streamed ribbon CPU storage remains stable");near(v[2].px,12);

        unsigned streamed=0;for(const auto&r:commands.resource_table().resources)streamed+=r.streamed_vertices;
        check(streamed==3,"bounded vertex batches opt into fenced streaming");
        std::cout<<"PASS original retail wheel widths and all three skid textures; bounded batches validate\n";
    }
    std::cout<<"PASS native wheel skid gates/width/opacity, gaps, materials and bounded pool\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
