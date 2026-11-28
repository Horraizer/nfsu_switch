#pragma once

#include "nfsu/draw_commands.h"
#include <cmath>
#include <cstring>

namespace nfsu {
// This is the fallback9 world.efx equation only. With this blend contract a
// zero sampled alpha is a color no-op; no depth or alpha-channel write can be
// removed. Other shaders and opaque/depth-writing/alpha-tested draws retain
// their existing fragment stage and early-depth behavior.
inline bool race_particle_zero_alpha_shader(const RecordedDraw& draw) {
    namespace sm=shader_manifest;
    const auto& state=draw.pipeline;
    return draw.material.fallback[3]==9 &&
        draw.program && std::strcmp(draw.program->name,"car_textured")==0 && draw.texture_resource!=kNoHandle &&
        draw.material.alpha[3]==0 && std::isfinite(draw.material.alpha[0]) &&
        state.blend && state.blend_src==sm::BlendFactor::src_alpha &&
        (state.blend_dst==sm::BlendFactor::one_minus_src_alpha || state.blend_dst==sm::BlendFactor::one) &&
        state.blend_op==sm::BlendOp::add && state.color_write==sm::ColorWrite::rgb &&
        !state.depth_write && state.alpha_test==sm::CompareOp::always;
}

inline bool race_particle_shader_transition(bool particle,const RecordedDraw* previous) {
    return !previous || particle!=race_particle_zero_alpha_shader(*previous);
}
} // namespace nfsu
