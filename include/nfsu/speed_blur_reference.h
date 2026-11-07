#pragma once
#include "nfsu/speed_blur.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace nfsu::diagnostic {
// Readback reference, never a race-timing measurement. Every intermediate is
// RGBA8, as in 40D370/40D090/40D6E0 and the owner's original filter effect.
struct SpeedBlurReference {
    std::array<std::vector<unsigned char>,5> image;
    std::array<unsigned,5> width{},height{};
};
inline SpeedBlurReference speed_blur_reference(const std::vector<unsigned char>& input,
        unsigned width,unsigned height,const SpeedBlurFrame& frame) {
    SpeedBlurReference result;
    if(width<2||height<2||input.size()!=std::size_t(width)*height*4)return result;
    result.width={width,width/2,width/2,width/2,width};
    result.height={height,height/2,height/2,height/2,height};
    result.image[0]=input;
    const auto sample=[](const std::vector<unsigned char>& image,unsigned w,unsigned h,
                         double u,double v,unsigned c) {
        const double px=std::clamp(u*w-0.5,0.0,double(w-1));
        const double py=std::clamp(v*h-0.5,0.0,double(h-1));
        const auto x=static_cast<unsigned>(px),y=static_cast<unsigned>(py);
        const auto x1=std::min(x+1,w-1),y1=std::min(y+1,h-1);
        const auto at=[&](unsigned xx,unsigned yy){return double(image[(std::size_t(yy)*w+xx)*4+c]);};
        const double a=at(x,y)+(at(x1,y)-at(x,y))*(px-x);
        const double b=at(x,y1)+(at(x1,y1)-at(x,y1))*(px-x);
        return a+(b-a)*(py-y);
    };
    const auto byte=[](double v){return static_cast<unsigned char>(std::clamp(std::floor(v+0.5),0.0,255.0));};
    for(unsigned stage=1;stage<5;++stage) {
        const unsigned w=result.width[stage],h=result.height[stage];
        const unsigned sw=result.width[stage-1],sh=result.height[stage-1];
        const auto& source=result.image[stage-1];auto& output=result.image[stage];
        output.resize(std::size_t(w)*h*4);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)for(unsigned c=0;c<4;++c) {
            const double u=double(x)/w,v=double(y)/h;double value=0;
            if(stage==1) {
                for(int sy:{-1,1})for(int sx:{-1,1})
                    value+=sample(source,sw,sh,u+sx*0.5/sw,v+sy*0.5/sh,c)*0.25;
            } else if(stage<4) {
                const double multiplier=stage==2?1.0:4.0;
                const double dx=((1-u)*frame.offsets[0]+u*frame.offsets[2])*multiplier;
                const double dy=((1-v)*frame.offsets[1]+v*frame.offsets[3])*multiplier;
                for(unsigned tap=0;tap<4;++tap)
                    value+=sample(source,sw,sh,u-tap*dx,v-tap*dy,c)*0.25;
            } else {
                const double alpha=double(frame.alpha)/255;
                value=input[(std::size_t(y)*w+x)*4+c]*(1-alpha)+sample(source,sw,sh,u,v,c)*alpha;
            }
            output[(std::size_t(y)*w+x)*4+c]=byte(value);
        }
    }
    return result;
}
} // namespace nfsu::diagnostic
