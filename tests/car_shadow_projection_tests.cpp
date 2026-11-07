#include "nfsu/car_shadow.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace nfsu;
void need(bool ok,const char* label){if(!ok)throw std::runtime_error(label);}
struct Surface { mutable unsigned calls=0; int mode=0; mutable float grid_z=0; mutable std::array<VehicleVec3,16> grid{}; };
bool road(const VehicleVec3& p,TrackContact& c,const void* user) {
    auto& s=*static_cast<const Surface*>(user);const unsigned n=s.calls++;
    if(s.mode==1 && n>=5)return false; // Grid misses, successful wheel planes.
    if(s.mode==2 && n>=1 && n<=12)return false; // No wheels, first two rows absent.
    if(s.mode==3)return false;
    c.point[0]=static_cast<float>(p.x);c.point[1]=static_cast<float>(p.y);
    c.point[2]=s.mode==4?static_cast<float>(p.x*0.2):0.F;
    if(s.mode==6 && n>=5)s.grid.at(n-5)=p;
    if(s.mode==5 && n>=5) {
        if(n==5)s.grid_z=float(p.z);
        else need(float(p.z)==s.grid_z,"original56ED31 constant grid query height despite body pitch");
    }
    c.normal[0]=s.mode==4?-0.196116135F:0.F;
    c.normal[2]=s.mode==4?0.980580676F:1.F;
    return true;
}
#include "car_shadow_ground_native.inc"
#include "car_shadow_grid_native.inc"
void original_ground_cache() {
    for(unsigned n=0;n<sizeof(shadow_ground_native)/sizeof(*shadow_ground_native);++n) {
        const auto& expected=shadow_ground_native[n];TrackContactWorld world;world.valid=true;
        world.collision.polygons.resize(expected.count);
        CarShadowGroundCache cache;cache.world=&world;cache.valid=true;
        cache.bounds={-100,-100,100,100};cache.count=expected.count;
        cache.last=expected.last==99?std::size_t(-1):expected.last;
        for(unsigned i=0;i<expected.count;++i) {
            auto& p=world.collision.polygons[i];p.unknown0[3]=static_cast<unsigned char>(expected.flags[i]);
            const float xy[4][2]={{-5,-5},{5,-5},{5,5},{-5,5}};
            for(unsigned k=0;k<4;++k){p.corner[k][0]=xy[k][0];p.corner[k][1]=xy[k][1];p.corner[k][2]=expected.heights[i];}
            cache.polygons[i]=i;
        }
        TrackContact hit;const bool found=query_car_shadow_ground(world,{expected.x,expected.y,expected.z},cache,hit);
        std::uint32_t height=0;std::memcpy(&height,&hit.point[2],4);
        const auto last=cache.last==std::size_t(-1)?99U:static_cast<unsigned>(cache.last);
        if(found!=expected.hit || (found&&(hit.polygon!=expected.polygon||height!=expected.height)) || last!=expected.last_after)
            throw std::runtime_error("original440A40 cache case differs: "+std::to_string(n));
        if(n==12)need(cache.polygons[0]==6,"original hit beyond index4 moves to front");
    }
}
void original_shadow_light() {
    // 56D700 executed in Unicorn against the authenticated USA 1.4 image.
    // Mixed XY checks its L1 normalization; shallow and steep cases guard
    // the argument order and both sides of the minimum elevation.
    struct Witness { VehicleVec3 origin, light, ray; };
    const Witness cases[] = {
        {{0,0,0},{-10,0,1},{7.98871756,0,-6.09757233}},
        {{0,0,0},{-10,3,1},{6.41311979,-1.92393601,-6.36344957}},
        {{0,0,0},{-1,0,10},{1,0,-10}},
        {{0,0,0},{1,5,7},{-1,-5,-7}},
        {{0,0,0},{0,0,9},{0,0,-9}},
        {{1670,-400,63},{-3900,650,1300},{3880.20751953,-731.45739746,-3519.95922852}},
        {{0,0,0},{-16384,0,7000},{14162.63574219,0,-10809.95703125}},
        {{0,0,0},{-10,0,7},{9.70307732,0,-7.40609694}},
        {{0,0,0},{-10,0,8},{10,0,-8}},
    };
    const auto matches = [](double value, double expected) {
        return std::abs(value-expected) <= 1.0e-6*(1.0+std::abs(expected));
    };
    for (const auto& witness : cases) {
        const auto ray = car_shadow_light_ray(witness.origin,witness.light);
        need(matches(ray.x,witness.ray.x) && matches(ray.y,witness.ray.y) &&
             matches(ray.z,witness.ray.z),"original56D700 light ray and minimum elevation");
    }
}
int main(){try {
    original_shadow_light();
    original_ground_cache();
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
    state.position.z=.3;surface={};
    need(project_car_shadow(asset,state,spec,road,&surface).alpha==204.F/255.F,"original56F037 truncates102.6 instead of rounding103");
    state.position.z=1.5;state.orientation={std::sqrt(.9),std::sqrt(.1),0,0};surface={};
    need(project_car_shadow(asset,state,spec,road,&surface).alpha==90.F/255.F,"original tilted45.6 alpha truncates45");
    for(const auto& native:shadow_grid_native) {
        state.orientation={native.q[0],native.q[1],native.q[2],native.q[3]};
        state.position={10,20,native.z};surface={0,6};
        const auto actual=project_car_shadow(asset,state,spec,road,&surface);
        need(std::abs(actual.alpha-native.alpha)<1e-7F,"original56F037 pose alpha");
        for(unsigned i=0;i<16;++i) {
            need(std::abs(surface.grid[i].x-native.grid[i][0])<.00001,"original56ED24 projected X");
            need(std::abs(surface.grid[i].y-native.grid[i][1])<.00001,"original56ED24 projected Y");
            need(float(surface.grid[i].z)==native.grid[i][2],"original56ED31 common query Z");
        }
    }
    state.position={0,0,0};
    state.orientation={1,0,0,0};
    state.position.z=3;surface={};need(project_car_shadow(asset,state,spec,road,&surface).alpha==0,"three-unit fade endpoint");
    state.position.z=0;surface={0,3};need(project_car_shadow(asset,state,spec,road,&surface).alpha==0,"missing reference refuses visible shadow");
    surface={0,4};auto slope=project_car_shadow(asset,state,spec,road,&surface);
    need(slope.road_heights[3]>slope.road_heights[0]+.9F,"separate terrain heights follow slope");
    state.orientation={std::cos(.2),0,std::sin(.2),0};state.position.z=.5;surface={0,5};
    project_car_shadow(asset,state,spec,road,&surface);
    need(surface.calls==21 && surface.grid_z==.5F,"tilted stock-rival tap domain");
    RecordedDraw draw;update_car_shadow_draw(slope,draw);
    for(unsigned i=0;i<4;++i){need(draw.material.fallback[i]==slope.road_heights[i],"GPU first row");need(draw.material.environment_color[i]==slope.road_heights[12+i],"GPU final row");}
    need(draw.material.alpha[1]==7,"GPU strip mask");
    std::cout<<"shadow projection tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
