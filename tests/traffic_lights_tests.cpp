#include "nfsu/light_glows.h"
#include "nfsu/car_parts.h"
#include "nfsu/frontend_motion.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace nfsu;
namespace {
struct NativeTrafficLightCase {unsigned kind,brake,head,tail;};
#include "traffic_lights_native.inc"
float value(unsigned bits) {float result;std::memcpy(&result,&bits,sizeof(result));return result;}
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
}
int main() {try {
    PreparedCarScene car;
    for(unsigned slot:{unsigned(kSlotBody),unsigned(kSlotHeadlight),unsigned(kSlotBrakelight)}) {
        PreparedInstance part;part.part_slot=slot;car.instances.push_back(part);
    }
    CarGlowAnchors anchors;
    for(auto& group:anchors)group.push_back({{0,0,0},true,{1,0,0}});
    CameraFrame camera;camera.eye={10,0,0};camera.forward={-1,0,0};camera.right={0,1,0};camera.up={0,0,1};
    const float point[]={0,0,0},toward[]={1,0,0},right[]={0,0,1},up[]={0,-1,0};
    unsigned cases=0;
    for(const auto& c:native_traffic_lights) {
        const auto lights=place_original_car_glows(anchors,car,camera,1280,value(c.brake),c.kind==3);
        require(lights.size()==7,"original light marker list changed");
        for(unsigned lamp=0;lamp<lights.size();++lamp) {
            const bool head=lamp<4;
            const unsigned descriptor=head?lamp%2:2;
            const unsigned texture=head?(descriptor==0?0:2):1;
            FrontendRoadGlow expected;
            const float gain=std::min(1.F,value(head?c.head:c.tail));
            require(frontend_car_glow(point,toward,right,up,1,10.F/640,0,descriptor,gain,expected),"native light primitive refused");
            require(lights[lamp].texture==texture,"service brake changed original glow texture selection");
            for(unsigned channel=0;channel<4;++channel) {
                if(lights[lamp].rgba[channel]!=expected.rgba[channel]) {
                    std::cerr<<"owner="<<c.kind<<" brake="<<value(c.brake)<<" lamp="<<lamp
                        <<" channel="<<channel<<" actual="<<unsigned(lights[lamp].rgba[channel])
                        <<" original="<<unsigned(expected.rgba[channel])<<'\n';
                    require(false,"traffic glow differs from original owner gain");
                }
            }
        }
        ++cases;
    }
    std::cout<<"PASS traffic light owner composition: "<<cases<<" original cases\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL traffic lights: "<<e.what()<<'\n';return 1;}}
