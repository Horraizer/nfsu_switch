#include "nfsu/car_shadow.h"
#include "nfsu/car_parts.h"
#include "nfsu/drive_session.h"
#include "nfsu/split_layout.h"
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
#include "car_shadow_view_native.inc"
#include "car_shadow_gc_native.inc"
void original_gc_shadow_view() {
    CarShadowView boundary;
    boundary.camera.eye={0,0,0};boundary.camera.forward={1,0,0};
    boundary.pixel_scale=car_shadow_view_pixel_scale(shadow_gc_boundary_fov);
    const int pixels=car_shadow_pixel_size(boundary,{shadow_gc_boundary_position,0,0});
    need(pixels==shadow_gc_boundary_pixels && car_shadow_silhouette_alpha(pixels,0)==55,
         "original GameCube CalculateH/GetPixelSize keeps the45-pixel LOD-B boundary");
    for(const auto& native:shadow_gc_scale_native) {
        const float scale=car_shadow_view_pixel_scale(static_cast<std::uint16_t>(native.fov));
        std::uint32_t bits=0;std::memcpy(&bits,&scale,sizeof bits);
        if(bits!=native.scale)
            throw std::runtime_error("original GameCube CalculateH bits differ at FOV "+std::to_string(native.fov));
    }
}
void original_shadow_view() {
    for(const auto& native:shadow_view_native) {
        CarShadowView view;
        view.camera.eye={0,0,0};view.camera.forward={1,0,0};
        view.pixel_scale=drive_view_pixel_scale(static_cast<std::uint16_t>(native.fov),native.width);
        view.two_player_views=native.two_players;
        need(std::abs(view.pixel_scale-native.scale)<=native.scale*1.e-6F,"original40DCB0 view scale");
        const VehicleVec3 position{native.position[0],native.position[1],native.position[2]};
        const int pixels=car_shadow_pixel_size(view,position);
        need(pixels==native.pixels,"original545180 radius-three size and split conversion");
        need(car_lod_for_pixel_size(float(pixels),native.two_players?1U:0U)==native.lod,"original56E0AD LOD");
        need(car_shadow_silhouette_alpha(pixels,native.height,native.two_players)==native.alpha,"original56E74C LOD alpha");
    }
    need(car_shadow_view_pixel_scale(0)==10.F,"GameCube zero-angle CalculateH fallback");
    for(bool split:{false,true}) {
        need(car_shadow_silhouette_alpha(120,0,split)==55,"LOD A/B retain full shadow alpha");
        need(car_shadow_silhouette_alpha(45,0,split)==55,"LOD B shadow endpoint");
        need(car_shadow_silhouette_alpha(44,0,split)==52,"LOD C starts below45 pixels");
        need(car_shadow_silhouette_alpha(32,0,split)==26,"LOD C middle fade uses pixels");
        need(car_shadow_silhouette_alpha(20,0,split)==0,"LOD C lower shadow endpoint");
        need(car_shadow_silhouette_alpha(19,0,split)==0,"LOD D omits silhouette");
        need(car_shadow_silhouette_alpha(45,1,split)==0,"height fade still removes airborne silhouette");
    }
    // Each player must use the current eye/direction and supplied view scale.
    CarShadowView first,second;
    first.camera.eye={0,0,0};first.camera.forward={1,0,0};first.pixel_scale=444.3363037109375F;
    first.two_player_views=true;second=first;second.camera.eye={80,0,0};second.camera.forward={-1,0,0};
    for(unsigned player:{1U,0U,1U}) {
        const auto& view=player?second:first;
        need(car_shadow_pixel_size(view,{50,0,0})==(player?34:19),"player-specific eye cannot reuse the previous view");
    }
    for(const auto layout:{SplitLayout::stacked,SplitLayout::side_by_side})for(unsigned player:{1U,0U,1U}) {
        ChaseCameraView chase;chase.eye={player?60.:0.,0,0};chase.heading=player?32768:0;
        chase.fov=player?16384:13020;
        const auto viewport=split_viewport(1280,720,layout,player);
        const auto lens=split_viewport(640,480,layout,player);
        OrbitCamera camera;std::string error;
        need(drive_render_camera(chase,ChaseFovMode::retail_widescreen,viewport.width,viewport.height,
            camera,error,double(lens.width)/lens.height),"actual split camera adapter");
        need(camera.native_fov==chase.fov,"widened split projection preserves the selected player's original FOV");
        CarShadowView view;view.two_player_views=true;
        need(make_camera_frame(camera,viewport.width,viewport.height,view.camera,error),"actual split camera basis");
        view.pixel_scale=car_shadow_view_pixel_scale(camera.native_fov);
        need(car_shadow_pixel_size(view,{40,0,0})==(player?31:19),"both split layouts keep the logical512 console scale and current player lens");
    }
}
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
    original_gc_shadow_view();
    original_shadow_view();
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
