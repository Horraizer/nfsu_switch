#include "nfsu/wheel_paint.h"
#include "nfsu/body_skin.h"
#include <string>
namespace nfsu {
std::uint32_t wheel_paint_pixel(std::uint32_t source,unsigned mask,std::uint32_t paint) noexcept {
    if(!mask)return source;
    std::uint32_t multiplied=0;
    for(unsigned k=0;k<4;++k) {
        const auto a=(source>>(8*k))&255U,b=(paint>>(8*k))&255U;
        const auto value=static_cast<unsigned>(double(b)*double(skin_weight_scale())*a);
        multiplied|=(value&255U)<<(8*k);
    }
    const float blend=float(double(mask)*skin_weight_scale());
    const float weights[]={1.F-blend,blend};
    const std::uint32_t colors[]={source,multiplied};std::uint32_t out=source;std::string error;
    if(!skin_blend(colors,weights,2,false,out,error))return source;
    return (out&0x00ffffffU)|(source&0xff000000U);
}
}
