#include "nfsu/car_shading.h"
#include "nfsu/scene.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace nfsu {
namespace {
std::uint32_t u32(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1])<<8) |
           (std::uint32_t(p[2])<<16) | (std::uint32_t(p[3])<<24);
}
float f32(const unsigned char* p) {
    const auto bits=u32(p); float value; std::memcpy(&value,&bits,4); return value;
}
std::uint32_t hash_name(const char* s) {
    std::uint32_t h=0xffffffffU;
    while (*s) h=h*33U+static_cast<unsigned char>(*s++);
    return h;
}
}
bool valid_car_shading_material(const CarShadingMaterial& c) noexcept {
    const float* vectors[]={c.diffuse_min,c.diffuse_range,c.specular_min,c.specular_range,
                           c.environment_min,c.environment_range,c.gates};
    for (const auto* v:vectors) for (int k=0;k<4;++k)
        if (!std::isfinite(v[k]) || std::abs(v[k])>4096) return false;
    for (int k=0;k<3;++k) if (c.gates[k]!=0 && c.gates[k]!=1) return false;
    return c.specular_min[3]>=0 && c.specular_min[3]<=128 &&
           c.specular_range[3]>=0 && c.specular_range[3]<=64;
}
bool parse_car_materials(const unsigned char* bytes, std::size_t size,
                         std::vector<CarMaterialRecord>& out, std::string& error) {
    out.clear(); error.clear();
    if ((!bytes && size) || size > (64U<<20)) { error="car materials: invalid input size"; return false; }
    std::vector<CarMaterialRecord> records;
    std::map<std::uint32_t,bool> seen;
    for (std::size_t at=0;at<size;) {
        if (size-at<8) { error="car materials: truncated chunk header"; return false; }
        const auto id=u32(bytes+at), length=u32(bytes+at+4); at+=8;
        if (length>size-at) { error="car materials: chunk exceeds input"; return false; }
        if (id==0x00135200U) {
            if (length!=160 || records.size()>=4096) { error="car materials: invalid record size/count"; return false; }
            const auto* p=bytes+at;
            CarMaterialRecord r; r.hash=u32(p+12);
            if (!seen.emplace(r.hash,true).second) { error="car materials: duplicate hash"; return false; }
            for (std::size_t i=0x30;i<160;i+=4) {
                const float value=f32(p+i);
                if (!std::isfinite(value) || value<0 || value>64) { error="car materials: coefficient out of range"; return false; }
            }
            auto& c=r.values;
            for (std::size_t i=0;i<3;++i) {
                c.diffuse_min[i]=f32(p+0x30)*f32(p+0x34+4*i);
                c.diffuse_range[i]=f32(p+0x40)*f32(p+0x44+4*i)-c.diffuse_min[i];
                c.specular_min[i]=f32(p+0x5c)*f32(p+0x60+4*i);
                c.specular_range[i]=f32(p+0x6c)*f32(p+0x70+4*i)-c.specular_min[i];
                c.environment_min[i]=0.5F*f32(p+0x80)*f32(p+0x84+4*i);
                c.environment_range[i]=0.5F*f32(p+0x90)*f32(p+0x94+4*i)-c.environment_min[i];
            }
            c.diffuse_min[3]=f32(p+0x50);
            c.diffuse_range[3]=f32(p+0x54)-c.diffuse_min[3];
            // Initial speed.exe global at 0x7070D4; later runtime mutation is untraced.
            c.specular_min[3]=1.1F*f32(p+0x58);
            c.specular_range[3]=f32(p+0x7c);
            c.gates[0]=c.gates[1]=c.gates[2]=1;
            records.push_back(r);
        }
        at+=length;
    }
    out=std::move(records); return true;
}
bool apply_car_materials(PreparedCarScene& scene, const unsigned char* bytes,
                         std::size_t size, bool blue_paint, std::string& error) {
    std::vector<CarMaterialRecord> records;
    if (!parse_car_materials(bytes,size,records,error)) return false;
    for (auto& draw:scene.draws) {
        auto hash=draw.material_key_hash;
        if (blue_paint && hash==hash_name("SKIN")) hash=hash_name("REGPAINTBLUE");
        draw.shading={};
        for (const auto& r:records) if (r.hash==hash) {
            draw.shading=r.values;
            const auto gates=material_coefficient_gates(draw.flags,true);
            draw.shading.gates[1]=gates.diffuse ? 1.0F : 0.0F;
            draw.shading.gates[2]=gates.environment ? 1.0F : 0.0F;
            break;
        }
    }
    return true;
}
} // namespace nfsu
