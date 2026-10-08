#include "nfsu/headlights.h"
#include "nfsu/texture_decode.h"
#include <cmath>

namespace nfsu {
bool load_headlight_texture(const std::filesystem::path& root,
                           RenderTexture& texture, std::string& error) {
    texture = {};
    std::vector<unsigned char> bytes;
    const auto packs = load_texture_pack_set(root/"Global/GLOBALB.BUN", bytes);
    const std::vector<std::uint32_t> wanted{0x28EEFA9CU}; // HEADLIGHTS, 0x40E740
    RenderTextureSource source;
    source.bytes=&bytes;source.wanted=&wanted;source.complete=packs.complete;
    for(const auto& pack:packs.packs)source.packs.push_back(&pack);
    RenderTextureSet decoded;
    if(!decode_render_texture_sources({source},{},decoded,error))return false;
    for(auto& t:decoded.textures)if(t.claims_hash(wanted[0])&&t.usable())texture=std::move(t);
    if(!texture.usable()){error="original HEADLIGHTS texture missing";return false;}
    return true;
}
bool prepare_headlight_projection(const RenderTexture& texture, const Vec3& origin,
    const Vec3& forward, const Vec3& up, HeadlightProjection& result, std::string& error) {
    result={};
    auto unit=[](Vec3& v){
        const double n=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
        if(!std::isfinite(n)||n<=0)return false;
        v.x/=n;v.y/=n;v.z/=n;return true;
    };
    Vec3 f=forward;f.y-=double(0.1F);
    if(!texture.usable() || !std::isfinite(origin.x) || !std::isfinite(origin.y) ||
       !std::isfinite(origin.z) || !unit(f)) {error="invalid headlight source or pose";return false;}
    // Original look-at uses world vertical, not the car's rolled up axis.
    Vec3 r{-f.z,0,f.x};
    if(!unit(r)){error="vertical headlight direction";return false;}
    Vec3 u{r.y*f.z-r.z*f.y,r.z*f.x-r.x*f.z,r.x*f.y-r.y*f.x};
    result.eye={origin.x+.75*up.x,origin.y+.75*up.y,origin.z+.75*up.z};
    if(!std::isfinite(result.eye.x)||!std::isfinite(result.eye.y)||!std::isfinite(result.eye.z)) {
        result={};error="invalid headlight up axis";return false;
    }
    // 0x40A4BD stores 2.5 radians as a 16-bit turn. 0x40DE19 uses
    // 0.6002401113510132 for view8's vertical angle, independently of screen.
    const auto fov=static_cast<unsigned>(2.5F*10430.3779296875F);
    const auto horizontal=fov/2;
    const auto vertical=static_cast<unsigned>(float(fov)*0.6002401113510132F*.5F);
    constexpr double turn=6.28318530717958647692/65536;
    result.focal_x=1/std::tan(horizontal*turn);
    result.focal_y=1/std::tan(vertical*turn);
    result.right=r;result.up=u;result.forward=f;result.texture=&texture;
    return true;
}
}
