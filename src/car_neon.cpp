#include "nfsu/car_neon.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/texture.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace nfsu {
std::uint32_t car_neon_color(const CarNeonSettings& settings,float seconds,float strength) noexcept {
    if(!std::isfinite(seconds)||!std::isfinite(strength))return 0;
    const auto sine=[](float x) {
        const float scaled=float(x*10430.3779296875F);
        const double phase=std::fmod(double(scaled),65536.);
        return player_angle_sin(static_cast<std::uint16_t>(static_cast<int>(phase)));
    };
    const float wave=std::abs(sine(float(seconds*4.699999809265137F)));
    int values[4];for(unsigned i=0;i<4;++i)values[i]=settings.base[i]+int(float(settings.delta[i])*wave);
    values[3]+=int(std::abs(sine(float(seconds*7.199999809265137F)))*std::abs(sine(float(seconds*6.099999904632568F)))*1.2000000476837158F);
    values[3]=int(float(values[3])*strength);
    std::uint32_t packed=0;for(unsigned i=0;i<4;++i)packed|=std::uint32_t(std::clamp(values[i],0,254))<<(8*i);
    return packed;
}
bool prepare_car_neon(const StockFrameInputs& inputs,const StockFrame& car,const CarShadowAsset& shadow,
                      CarNeonAsset& out,std::string& error) {
    out={};const auto index=car.report.slots[116].record;
    if(index==kNoCarPart)return true;
    const auto& part=car.parts.records.at(index);if(!part.upgrade_level)return true;
    CarNeonAsset next;next.enabled=true;next.ground=shadow;
    const char* fields[]={"RED","GREEN","BLUE","ALPHA"};
    const auto attribute=[&](const std::string& name,int& value) {
        const auto key=bin_hash(name);
        for(int i=0;i<part.attribute_count;++i) {
            const auto& a=car.parts.attributes[part.attribute_index+unsigned(i)];
            if(a.key==key&&a.value<=255){value=int(a.value);return true;}
        }
        return false;
    };
    for(unsigned i=0;i<4;++i) {
        int second=0;
        if(!attribute(fields[i],next.color.base[i])||!attribute(std::string(fields[i])+"2",second)) {
            error="installed neon color attributes unavailable";return false;
        }
        next.color.delta[i]=second-next.color.base[i];
    }
    const auto name=car.report.car_type_name+"_NEON";const auto hash=bin_hash(name);std::vector<std::uint32_t> wanted{hash};
    RenderTextureSource source;source.bytes=&inputs.file(StockFrameFile::car_textures).bytes;source.packs.push_back(&car.car_pack);source.wanted=&wanted;
    RenderTextureSet textures;RenderTextureLimits limits;limits.retain_mips=true;
    if(!decode_render_texture_sources({source},limits,textures,error))return false;
    if(textures.textures.size()!=1||!textures.textures[0].usable()||textures.textures[0].hash!=hash) {
        error="installed neon texture unavailable: "+name;return false;
    }
    next.ground.texture=std::move(textures.textures[0]);next.ground.texture_name=name;
    out=std::move(next);error.clear();return true;
}
void update_car_neon_draw(const CarNeonSettings& settings,float seconds,const CarShadowPlacement& placement,RecordedDraw& draw) {
    update_car_shadow_draw(placement,draw);
    const auto color=car_neon_color(settings,seconds,placement.strength);
    draw.material.alpha[0]=2.F*float(color>>24)/255;
    draw.material.alpha[2]=float(color&0xffffffU);draw.material.alpha[3]=7;
}
bool record_car_neon_commands(const CarNeonAsset& asset,float seconds,const CarShadowPlacement& placement,
                              const RenderOptions& options,RecordedCommands& out,std::string& error) {
    out={};if(!asset.enabled)return true;
    if(!record_car_shadow_commands(asset.ground,placement,options,out,error))return false;
    auto& draw=out.owning_draws()[0];draw.name="car underbody neon";
    const char* blend=render_blend_mode_of(asset.ground.texture.alpha_blend_mode)==RenderBlendMode::additive?"additive":"source_over";bool found=false;
    for(std::size_t i=0;i<draw.program->variant_count;++i)if(!std::strcmp(draw.program->variants[i].name,blend)) {
        draw.pipeline=draw.program->variants[i];found=true;break;
    }
    if(!found){error="neon authored blend variant absent";return false;}
    draw.pipeline.depth_write=false;draw.pipeline.cull=shader_manifest::Cull::none;
    update_car_neon_draw(asset.color,seconds,placement,draw);return true;
}
bool render_car_neon(const CarNeonAsset& asset,float seconds,const CarShadowPlacement& placement,
                     const RenderOptions& options,Frame& frame,RenderStats& stats,std::string& error) {
    if(!asset.enabled)return true;const auto color=car_neon_color(asset.color,seconds,placement.strength);
    if(!(color>>24))return true;
    CameraFrame camera;if(!make_camera_frame(options.camera,frame.width(),frame.height(),camera,error))return false;
    RasterMaterial material;material.texture=&asset.ground.texture;material.blend=render_blend_mode_of(asset.ground.texture.alpha_blend_mode)==RenderBlendMode::additive?RenderBlendMode::additive:RenderBlendMode::source_over;
    material.depth_write=false;material.alpha_scale=2.F*float(color>>24)/255;material.linear_filter=true;
    auto tinted=options;tinted.vertex_color=true;tinted.car_shading=false;Rasterizer raster(frame,tinted,{},stats);
    for(const auto& triangle:asset.ground.triangles) {
        if(placement.projected&&!(placement.strip_mask&(1U<<(triangle.a/4))))continue;
        ClipVertex clip[3];const unsigned indices[]={triangle.a,triangle.b,triangle.c};
        for(unsigned k=0;k<3;++k) {
            const auto& v=asset.ground.vertices[indices[k]];auto world=placed_vertex(v,placement.transform.rows);
            if(placement.projected)world.y=placement.road_heights[indices[k]];
            clip[k]=project_point(camera,world);clip[k].u=v.u;clip[k].v=v.v;
            clip[k].r=2.F*float(color&255);clip[k].g=2.F*float((color>>8)&255);clip[k].b=2.F*float((color>>16)&255);
        }
        if(!raster.draw_triangle(clip,material,std::uint32_t(stats.triangles_submitted++),error))return false;
    }
    return true;
}
}
