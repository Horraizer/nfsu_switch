#include "nfsu/car_shading.h"
#include "nfsu/car_parts.h"
#include "nfsu/scene.h"
#include <algorithm>
#include <cmath>
#include <algorithm>
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
std::uint32_t select_car_paint_material_hash(const CarPartDatabase& database,
    std::size_t installed_base_paint, std::optional<std::uint32_t> skin_color) {
    if (!database.ok()) return 0;
    std::size_t chosen=installed_base_paint;
    if (skin_color) {
        std::uint64_t best=~std::uint64_t{0};
        chosen=kNoCarPart;
        for (const auto& part:database.records) {
            if (part.slot!=30 || part.name.rfind("GLOSS_L1_COLOR",0)!=0) continue;
            std::uint32_t color=0;
            unsigned found=0;
            for (std::int16_t i=0;i<part.attribute_count;++i) {
                const auto& a=database.attributes[part.attribute_index+static_cast<std::size_t>(i)];
                unsigned shift=0;
                if (a.key==hash_name("RED")) { shift=0; found|=1; }
                else if (a.key==hash_name("GREEN")) { shift=8; found|=2; }
                else if (a.key==hash_name("BLUE")) { shift=16; found|=4; }
                else if (a.key==hash_name("GLOSS")) { shift=24; found|=8; }
                else continue;
                color|=(a.value&255U)<<shift;
            }
            if (found!=15) continue;
            std::uint64_t distance=0;
            for (unsigned shift:{0U,8U,16U,24U}) {
                const auto delta=static_cast<int>((color>>shift)&255U)-
                                 static_cast<int>((*skin_color>>shift)&255U);
                distance+=static_cast<std::uint64_t>(delta*delta);
            }
            if (distance<best) { best=distance; chosen=part.index; }
        }
    }
    if (chosen>=database.records.size()) return 0;
    const auto& part=database.records[chosen];
    for (std::int16_t i=0;i<part.attribute_count;++i) {
        const auto& a=database.attributes[part.attribute_index+static_cast<std::size_t>(i)];
        if (a.key==0x6BA02C05U) return a.value;
    }
    return 0;
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
                         std::size_t size, std::uint32_t paint_hash, std::string& error) {
    std::vector<CarMaterialRecord> records;
    if (!parse_car_materials(bytes,size,records,error)) return false;
    const auto wanted=paint_hash;
    bool found=false;
    for (const auto& r:records) if (r.hash==wanted) { found=true; break; }
    const auto selected=found ? wanted : hash_name("DEFAULT");
    for (auto& draw:scene.draws) {
        auto hash=draw.material_key_hash;
        // 0x56F520/0x56F5A0 bind CARSKIN to the selected BASE_PAINT material.
        if (hash==hash_name("CARSKIN")) hash=selected;
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

namespace nfsu {
FrontendLighting frontend_startup_lighting() noexcept {
    // Authentic 542E40 -> 544390 witness: mode 3, actual primary CView and
    // reflection basis, 735548/73554C zero, road light RGB zero and no race
    // light list. Those unresolved startup inputs remain explicit assumptions.
    const float values[10][2]={
        {.35557228326797485F,.49166592955589294F},
        {-.03482309728860855F,-.04815147817134857F},
        {.03482309728860855F,.04815147817134857F},
        {-.07494521141052246F,-.10363014787435532F},
        {-.028591757640242577F,-.039535120129585266F},
        {.04231078550219536F,.058505043387413025F},
        {.09493815898895264F,.13127529621124268F},
        {.22944235801696777F,.31726035475730896F},
        {.16954396665096283F,.23443613946437836F},
        {.32748597860336304F,.4528297185897827F}};
    FrontendLighting result;
    for(unsigned i=0;i<10;++i) {
        for(unsigned c=0;c<3;++c) result.harmonic[i][c]=values[i][0];
        result.harmonic[i][3]=values[i][1];
    }
    return result;
}
void frontend_local_eye(const float* placement,const double* raw_eye,float (&out)[4]) noexcept {
    // 542C20: subtract translation then transpose the placement 3x3.
    // Both independently read native clamp endpoints equal 2.7799999714.
    double delta[3],local[3];
    for(unsigned i=0;i<3;++i)delta[i]=raw_eye[i]-(placement?placement[12+i]:0);
    double squared=0;
    for(unsigned i=0;i<3;++i) {
        local[i]=placement ? placement[i*4]*delta[0]+placement[i*4+1]*delta[1]+
            placement[i*4+2]*delta[2] : delta[i];
        squared+=local[i]*local[i];
    }
    const double scale=squared>0 ? 2.7799999713897705/std::sqrt(squared) : 0;
    for(unsigned i=0;i<3;++i)out[i]=static_cast<float>(local[i]*scale);
    out[3]=1;
}
void frontend_surface(const FrontendLighting& lighting,const CarShadingMaterial& c,
    const float* position,const float* normal,const float* eye,double (&out)[7]) noexcept {
    double view[3],squared=0;
    for(unsigned i=0;i<3;++i){view[i]=double(eye[i])-position[i];squared+=view[i]*view[i];}
    const double length=std::sqrt(squared);
    for(auto& value:view)value=length>0 ? value/length : 0;
    double v=0;for(unsigned i=0;i<3;++i)v+=view[i]*normal[i];
    v=std::max(.01,v); // car.efx neither flips normals nor clamps the upper bound.
    double reflected[3];for(unsigned i=0;i<3;++i)reflected[i]=2*v*normal[i]-view[i];
    const auto harmonic=[&](const auto* n,unsigned channel) {
        const double terms[10]={1,n[0]*n[0],n[1]*n[1],n[2]*n[2],
            n[0]*n[1],n[2]*n[0],n[1]*n[2],n[0],n[1],n[2]};
        double value=0;for(unsigned i=0;i<10;++i)value+=lighting.harmonic[i][channel]*terms[i];
        return value;
    };
    const double spec_v=std::pow(v,double(c.specular_min[3]));
    const double spec_luma=std::pow(std::clamp(harmonic(reflected,3),0.0,1.0),double(c.specular_min[3]));
    for(unsigned i=0;i<3;++i) {
        out[i]=c.gates[1]*harmonic(normal,i)*(c.diffuse_min[i]+v*c.diffuse_range[i]);
        out[3+i]=.5*c.gates[1]*harmonic(reflected,i)*spec_luma*
            (c.specular_min[i]+spec_v*c.specular_range[i]);
    }
    // The native pixel shader multiplies interpolated diffuse alpha and
    // interpolated specular RGB separately, rather than at each vertex.
    out[6]=c.gates[1]*(c.diffuse_min[3]+v*c.diffuse_range[3]);
    // EnvMap is a separate live capture consumer, not a fabricated sky band.
}
} // namespace nfsu
