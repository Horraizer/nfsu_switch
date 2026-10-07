#pragma once
#include "nfsu/draw_commands.h"
#include <cmath>
#include <cstring>
namespace nfsu {
// Main-view FE glow specialization only. Keep capture and generic materials
// on their original path. Every eliminated shader term has a zero coefficient.
inline bool frontend_glow_fast(const RecordedDrawView& draw,
                               const DrawFrameUniform& frame) {
    const auto& d=draw.definition;const auto& m=draw.material;
    if(d.frontend_sprite_view!=1 || !d.program ||
       std::strcmp(d.program->name,"car_textured") ||
       d.texture_resource==kNoHandle || frame.options[3]!=0 ||
       d.pipeline.alpha_test!=shader_manifest::CompareOp::always ||
       (m.environment_range[3]!=1 && m.environment_range[3]!=2) ||
       m.environment_min[3]!=0 || m.alpha[1]!=0 || m.alpha[2]!=0 || m.alpha[3]!=0 ||
       !(m.alpha[0]>=0 && m.alpha[0]<=1) ||
       !(m.environment_color[3]>=0 && m.environment_color[3]<=8))return false;
    for(unsigned i=0;i<3;++i)
        if(m.environment_min[i]!=0 || m.environment_range[i]!=0 ||
           !(m.environment_color[i]>=0 && m.environment_color[i]<=255))return false;
    for(float x:draw.placement.rows)if(!std::isfinite(x))return false;
    return true;
}
inline bool frontend_glow_no_output(const RecordedDrawView& draw,
                                    const DrawFrameUniform& frame) {
    const auto& p=draw.definition.pipeline;
    return frontend_glow_fast(draw,frame) && draw.material.alpha[0]==0 &&
        !p.depth_write && p.color_write==shader_manifest::ColorWrite::rgb &&
        p.blend && p.blend_op==shader_manifest::BlendOp::add &&
        p.blend_src==shader_manifest::BlendFactor::src_alpha &&
        p.blend_dst==shader_manifest::BlendFactor::one;
}
} // namespace nfsu
