#include "nfsu/car_shadow.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace nfsu;
void need(bool ok,const char* label){if(!ok)throw std::runtime_error(label);}
struct Surface { mutable unsigned calls=0; int mode=0; };
bool road(const VehicleVec3& p,TrackContact& c,const void* user) {
    auto& s=*static_cast<const Surface*>(user);const unsigned n=s.calls++;
    if(s.mode==1 && n>=5)return false; // Grid misses, successful wheel planes.
    if(s.mode==2 && n>=1 && n<=12)return false; // No wheels, first two rows absent.
    if(s.mode==3)return false;
    c.point[0]=static_cast<float>(p.x);c.point[1]=static_cast<float>(p.y);
    c.point[2]=s.mode==4?static_cast<float>(p.x*0.2):0.F;
    c.normal[0]=s.mode==4?-0.196116135F:0.F;
    c.normal[2]=s.mode==4?0.980580676F:1.F;
    return true;
}
int main(){try {
    CarShadowAsset asset;asset.selected_bounds=true;asset.low={-2.F,-1.F,0.F};asset.high={3.F,1.F,1.F};
    for(unsigned i=0;i<16;++i){asset.vertices[i].px=-1.F+2.F*(i%4)/3.F;asset.vertices[i].pz=-1.F+2.F*(i/4)/3.F;}
    VehicleSpec spec;spec.handling.centre_of_mass={0,0,0};VehicleState state;state.orientation={1,0,0,0};
    Surface surface;
    auto p=project_car_shadow(asset,state,spec,road,&surface);
    need(surface.calls==21 && p.strip_mask==7,"all original terrain taps and strips");
    need(std::abs(p.alpha-228.F/255.F)<1e-6,"quantized full alpha");
    need(std::abs(p.transform.rows[12]-.525F)<1e-6,"asymmetric authored bounds retain center");
    for(unsigned i=0;i<16;++i)need(std::abs(p.road_heights[i]-((i==5||i==6||i==9||i==10)?.12F:.04F))<1e-6,"four inner lifts only");
    surface={0,1};auto fallback=project_car_shadow(asset,state,spec,road,&surface);
    need(fallback.road_heights==p.road_heights && fallback.strip_mask==7,"wheel-plane extrapolation after grid misses");
    surface={0,2};auto clipped=project_car_shadow(asset,state,spec,road,&surface);
    need(clipped.strip_mask==4,"omit strips with four rejected taps; retain final strip");
    state.position.z=1.5;surface={};auto airborne=project_car_shadow(asset,state,spec,road,&surface);
    need(std::abs(airborne.alpha-114.F/255.F)<1e-6 && airborne.strip_mask==7,"low airborne fade independent of wheel contact");
    state.position.z=3;surface={};need(project_car_shadow(asset,state,spec,road,&surface).alpha==0,"three-unit fade endpoint");
    state.position.z=0;surface={0,3};need(project_car_shadow(asset,state,spec,road,&surface).alpha==0,"missing reference refuses visible shadow");
    surface={0,4};auto slope=project_car_shadow(asset,state,spec,road,&surface);
    need(slope.road_heights[3]>slope.road_heights[0]+.9F,"separate terrain heights follow slope");
    RecordedDraw draw;update_car_shadow_draw(slope,draw);
    for(unsigned i=0;i<4;++i){need(draw.material.fallback[i]==slope.road_heights[i],"GPU first row");need(draw.material.environment_color[i]==slope.road_heights[12+i],"GPU final row");}
    need(draw.material.alpha[1]==7,"GPU strip mask");
    std::cout<<"shadow projection tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
