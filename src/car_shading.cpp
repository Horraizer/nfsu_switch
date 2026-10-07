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
std::uint32_t car_part_material_hash(const CarPartDatabase& database,
                                     std::size_t record) {
    if (!database.ok() || record>=database.records.size()) return 0;
    const auto& part=database.records[record];
    for (std::int16_t i=0;i<part.attribute_count;++i) {
        const auto& a=database.attributes[part.attribute_index+static_cast<std::size_t>(i)];
        if (a.key==0x6BA02C05U) return a.value;
    }
    return 0;
}
std::uint32_t select_car_paint_material_hash(const CarPartDatabase& database,
    std::size_t installed_base_paint, std::optional<std::uint32_t> skin_color) {
    if (!database.ok()) return 0;
    // 0x56B4F2..0x56B528 keeps the installed BASE_PAINT record's own
    // material; the nearest-swatch surrogate below stands in only for a
    // color-only skin that installed no paint record.
    std::size_t chosen=installed_base_paint;
    if (car_part_material_hash(database,chosen)==0) {
        chosen=kNoCarPart;
        if (skin_color) {
            std::uint64_t best=~std::uint64_t{0};
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
    }
    if (chosen>=database.records.size()) return 0;
    return car_part_material_hash(database,chosen);
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
                         std::size_t size, std::uint32_t paint_hash,
                         std::uint32_t window_tint_hash, std::string& error) {
    std::vector<CarMaterialRecord> records;
    if (!parse_car_materials(bytes,size,records,error)) return false;
    const auto wanted=paint_hash;
    bool found=false;
    for (const auto& r:records) if (r.hash==wanted) { found=true; break; }
    const auto selected=found ? wanted : hash_name("DEFAULT");
    bool tint_found=false;
    if (window_tint_hash!=0)
        for (const auto& r:records) if (r.hash==window_tint_hash) { tint_found=true; break; }
    const auto tint=tint_found ? window_tint_hash : 0;
    for (auto& draw:scene.draws) {
        auto hash=draw.material_key_hash;
        // 0x56F520/0x56F5A0 bind CARSKIN to the selected BASE_PAINT material.
        if (hash==hash_name("CARSKIN")) hash=selected;
        // 0x56F5B1..0x56F5E1 replace the production WINDSHIELD material with
        // the installed tint's selected material, when one is installed.
        if (tint!=0 && hash==hash_name("WINDSHIELD")) hash=tint;
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
FrontendLighting frontend_point_lighting(const FrontendPointLight* lights,std::size_t count,
    const float (&car)[3]) noexcept {
    struct Input {float direction[3],color[4];};Input input[14]{};unsigned used=3;
    // Genuine542E40 normalized FE table directions under the recovered,
    // fixed main-menu reflection basis. Remaining table intensities are zero.
    const float directions[3][4]={
        {.12326809763908386F,.829439640045166F,.5448254942893982F,221.82000732421875F},
        {.9293131232261658F,-.3390888571739197F,.1462731659412384F,144.58999633789062F},
        {-.37582290172576904F,-.5062289834022522F,.7762018442153931F,67.66000366210938F}};
    for(unsigned i=0;i<3;++i) {
        for(unsigned c=0;c<3;++c){input[i].direction[c]=directions[i][c];input[i].color[c]=directions[i][3];}
    }
    // Genuine5411B0 range/inner-radius selector, in stable road pool order.
    for(std::size_t i=0;lights && i<std::min<std::size_t>(count,32) && used<14;++i) {
        const auto& light=lights[i];float delta[3];double squared=0;
        for(unsigned c=0;c<3;++c){delta[c]=light.position[c]-car[c];squared+=double(delta[c])*delta[c];}
        const double radius=double(light.radius)*light.radius;
        if(!(squared<radius))continue;
        const double inner=double(light.attenuation[0])*light.attenuation[0];
        const float weight=static_cast<float>(squared>inner?1-(squared-inner)/(radius-inner):1);
        const double length=std::sqrt(squared);auto& dst=input[used++];
        for(unsigned c=0;c<3;++c) {
            dst.direction[c]=length>0?float(delta[c]/length):(c==0?1.F:0.F);
            dst.color[c]=light.rgb[c]*1.5F*weight;
        }
    }
    float energy=0;
    for(unsigned i=0;i<used;++i) {
        auto& color=input[i].color;
        for(unsigned c=0;c<3;++c)color[c]*=.003921568859368563F;
        color[3]=color[2]*.17000000178813934F+color[0]*.28999999165534973F+color[1]*.5400000214576721F;
        energy+=color[3];
    }
    // Genuine542DC0 sixth-order normalization polynomial, then1/poly^4.
    const long double x=energy,x3=x*x*x,x4=x3*x,x5=static_cast<float>(x4)*x;
    const long double poly=x5*x*.0000006906000180606497F+x5*.000005430199962574989F+
        x4*.00017156200192403048F+x3*.002591371303424239F+
        static_cast<float>(.03125758469104767F*x*x+x*.24999868869781494F+1);
    const long double gain=energy>2?2.L/energy:1-1/(poly*poly*poly*poly);
    FrontendLighting result;
    for(unsigned i=0;i<used;++i) {
        for(unsigned sign=0;sign<2;++sign) {
            const float direction_scale=sign?-1.F:1.F,color_scale=sign?-.05000000074505806F:1.F;
            const float a=input[i].direction[0]*direction_scale,b=input[i].direction[1]*direction_scale,
                c=input[i].direction[2]*direction_scale;
            for(unsigned channel=0;channel<4;++channel) {
                const float color=static_cast<float>(input[i].color[channel]*gain)*color_scale;
                const float c6=color*.48860299587249756F,c7=color*1.0925480127334595F,
                    c9=color*.31539198756217957F*(c*c*3-1),c8=color*.5462740063667297F;
                const float values[10]={.2820949852466583F*color*.886227011680603F-c9*.24770799279212952F,
                    c8*(a*a-b*b)*.4290429949760437F,-c8*(a*a-b*b)*.4290429949760437F,
                    c9*.7431250214576721F,b*a*c7*.8580859899520874F,a*c*c7*.8580859899520874F,
                    c7*b*c*.4290429949760437F,c6*a*1.0233279466629028F,
                    c6*b*1.0233279466629028F,c6*c*1.0233279466629028F};
                for(unsigned term=0;term<10;++term)result.harmonic[term][channel]+=values[term];
            }
        }
    }
    // Genuine541FC0:735548 zero leaves RGB unnormalized, specular luma
    // is normalized to original6F3E0C. These startup flags remain assumptions.
    double squared=0;for(const auto& vector:result.harmonic)squared+=double(vector[3])*vector[3];
    if(squared>0)for(auto& vector:result.harmonic)vector[3]=float(vector[3]*.800000011920929/std::sqrt(squared));
    return result;
}
FrontendLighting frontend_startup_lighting() noexcept {
    const float origin[3]={0,0,.20027998089790344F};
    return frontend_point_lighting(nullptr,0,origin);
}
bool frontend_cube_coordinates(const double (&d)[3],unsigned& face,double& u,double& v) noexcept {
    for(double value:d)if(!std::isfinite(value))return false;
    double major,sx,sy;
    if(std::abs(d[0])>=std::abs(d[1]) && std::abs(d[0])>=std::abs(d[2])) {
        major=std::abs(d[0]);face=d[0]>=0?0:1;sx=d[0]>=0?-d[2]:d[2];sy=-d[1];
    } else if(std::abs(d[1])>=std::abs(d[2])) {
        major=std::abs(d[1]);face=d[1]>=0?2:3;sx=d[0];sy=d[1]>=0?d[2]:-d[2];
    } else {major=std::abs(d[2]);face=d[2]>=0?4:5;sx=d[2]>=0?d[0]:-d[0];sy=-d[1];}
    if(major==0)return false;
    u=std::clamp((sx/major+1)*.5,.5/128,127.5/128);
    v=std::clamp((sy/major+1)*.5,.5/128,127.5/128);
    return true;
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
    const float* position,const float* normal,const float* eye,double (&out)[7],const float* placement) noexcept {
    double view[3],squared=0;
    for(unsigned i=0;i<3;++i){view[i]=double(eye[i])-position[i];squared+=view[i]*view[i];}
    const double length=std::sqrt(squared);
    for(auto& value:view)value=length>0 ? value/length : 0;
    double v=0;for(unsigned i=0;i<3;++i)v+=view[i]*normal[i];
    v=std::max(.01,v); // car.efx neither flips normals nor clamps the upper bound.
    double reflected[3];for(unsigned i=0;i<3;++i)reflected[i]=2*v*normal[i]-view[i];
    const auto harmonic=[&](const auto* local,unsigned channel) {
        // Genuine542400/541C90 transforms the harmonic polynomial for each
        // placement. Contracting the shared polynomial with the transformed
        // direction is algebraically the same operation (native oracle).
        double n[3];
        for(unsigned axis=0;axis<3;++axis)n[axis]=placement?
            local[0]*placement[axis]+local[1]*placement[4+axis]+local[2]*placement[8+axis]:local[axis];
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
    // EnvMap is sampled separately per fragment, not a fabricated sky band.
}
} // namespace nfsu
