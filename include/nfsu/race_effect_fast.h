#pragma once
#include "nfsu/draw_commands.h"
#include <cmath>
#include <cstring>
namespace nfsu {
// Private GPU markers only: 4 is intrinsically unlit, 5 retains flat lighting.
// Eligibility depends on immutable material coefficients, not frame options.
// This also makes a static package safe across cached command-list reuse.
inline unsigned race_effect_fast_marker(const RecordedDraw& d,
                                        const DrawMaterialUniform& m) {
    if(d.frontend_sprite_view || !d.program || d.texture_resource==kNoHandle ||
       (std::strcmp(d.program->name,"car_textured") &&
        std::strcmp(d.program->name,"track_textured") &&
        std::strcmp(d.program->name,"track_linear_textured")) ||
       !(m.fallback[3]>=0 && m.fallback[3]<=2) ||
       (m.environment_range[3]!=0 && m.environment_range[3]!=1 && m.environment_range[3]!=2) ||
       m.environment_min[3]!=0 || m.alpha[1]!=0 || m.alpha[2]!=0 || m.alpha[3]!=0 ||
       !(m.alpha[0]>=0 && m.alpha[0]<=1) ||
       !(m.environment_color[3]>=0 && m.environment_color[3]<=8))return 0;
    for(unsigned i=0;i<3;++i)
        if(m.environment_min[i]!=0 || m.environment_range[i]!=0 ||
           !(m.environment_color[i]>=0 && m.environment_color[i]<=255))return 0;
    return m.fallback[3]>=1.5F || m.environment_range[3]>=.5F ? 4U : 5U;
}
inline DrawMaterialUniform race_effect_gpu_material(const RecordedDraw& d,
                                                    const DrawMaterialUniform& source) {
    auto result=source;
    const auto marker=race_effect_fast_marker(d,source);
    if(marker)result.fallback[3]=static_cast<float>(marker);
    return result;
}
inline bool race_effect_no_output(const RecordedDrawView& draw) {
    const auto& p=draw.definition.pipeline;
    if(!race_effect_fast_marker(draw.definition,draw.material) || draw.material.alpha[0]!=0 ||
       p.depth_write || p.color_write!=shader_manifest::ColorWrite::rgb ||
       !p.blend || p.blend_op!=shader_manifest::BlendOp::add ||
       p.blend_src!=shader_manifest::BlendFactor::src_alpha ||
       (p.blend_dst!=shader_manifest::BlendFactor::one &&
        p.blend_dst!=shader_manifest::BlendFactor::one_minus_src_alpha))return false;
    for(float x:draw.placement.rows)if(!std::isfinite(x))return false;
    return true;
}
} // namespace nfsu
