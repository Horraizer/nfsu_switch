#pragma once
#include "nfsu/draw_commands.h"
#include <cstring>

namespace nfsu {
struct DekoDrawBindings {
    bool shaders, pipeline, layout, texture, vertex, index;
};

// Valid only within one uninterrupted draw pass. A null predecessor forces
// all bindings after a render-target/list/probe boundary. Uniforms remain
// per draw: equal definitions can carry different live transforms/materials.
inline DekoDrawBindings deko_draw_bindings(const RecordedDraw& draw,
                                          const RecordedDraw* previous) {
    if (!previous) return {true,true,true,true,true,true};
    const bool program = draw.program != previous->program;
    const bool textured = draw.texture_resource != kNoHandle;
    const bool was_textured = previous->texture_resource != kNoHandle;
    const bool cutout = draw.pipeline.alpha_test != shader_manifest::CompareOp::always;
    const bool was_cutout = previous->pipeline.alpha_test != shader_manifest::CompareOp::always;
    // Padding equality is conservative: at worst it requests an extra bind.
    const bool pipeline = std::memcmp(&draw.pipeline, &previous->pipeline,
                                      sizeof draw.pipeline) != 0;
    return {program || textured != was_textured || cutout != was_cutout,
            pipeline, program, program || draw.texture_resource != previous->texture_resource,
            draw.vertex_resource != previous->vertex_resource,
            draw.index_resource != previous->index_resource};
}
} // namespace nfsu
