#pragma once
#include "nfsu/draw_commands.h"
#include <cstring>

// Independent comparison with observations from the CPU submission loop.
// Returns the first named field that differs; used for synthetic and retail.
inline std::string compare_draws(const nfsu::RecordedCommands& commands,
                                 const std::vector<nfsu::ReferenceDraw>& trace,
                                 bool track) {
    namespace sm = nfsu::shader_manifest;
    std::string error;
    if (!nfsu::validate_recorded_commands(commands, error)) return error;
    if (commands.owning_draws().size() != trace.size()) return "draw count";
    for (std::size_t i = 0; i < trace.size(); ++i) {
        const auto& d = commands.owning_draws()[i];
        const auto& r = trace[i];
        const auto& p = d.pipeline;
        const auto fail = [&](const char* field) { return d.name + ": " + field; };
        if (d.group != r.group || d.mesh != r.mesh || d.instance != r.instance) return fail("draw order/identity");
        if (d.first_index != r.first_index || d.index_count != r.index_count) return fail("index range");
        if (d.first_vertex != 0) return fail("first vertex");
        if (d.vertex_resource >= commands.owning_uploads().resources.size() ||
            d.index_resource >= commands.owning_uploads().resources.size()) return fail("buffer handles");
        const auto& v = commands.owning_uploads().resources[d.vertex_resource];
        const auto& idx = commands.owning_uploads().resources[d.index_resource];
        if (v.kind != nfsu::UploadResourceKind::vertex_buffer || v.source != r.mesh ||
            v.elements != d.vertex_count) return fail("vertex binding");
        if (idx.kind != nfsu::UploadResourceKind::index_buffer || idx.source != r.mesh ||
            idx.element_bytes != 2 || std::uint64_t{d.first_index} + d.index_count > idx.elements) return fail("index binding");
        if (r.texture == nfsu::kNoHandle) {
            if (d.texture_resource != nfsu::kNoHandle) return fail("fallback texture binding");
        } else {
            if (d.texture_resource >= commands.owning_uploads().resources.size()) return fail("texture handle");
            const auto& t = commands.owning_uploads().resources[d.texture_resource];
            if (t.kind != nfsu::UploadResourceKind::texture || t.source != r.texture) return fail("texture binding");
        }
        const bool textured = r.texture != nfsu::kNoHandle;
        const char* program = track ? (textured ? "track_textured" : "track_fallback")
                                    : (textured ? "car_textured" : "car_fallback");
        if (!d.program || std::strcmp(d.program->name, program)) return fail("shader program");
        if (d.program->buffer_count != 1 || d.program->buffers[0].stride != v.element_bytes ||
            d.program->buffers[0].type_size != v.element_bytes) return fail("vertex layout stride");
        for (std::size_t a = 0; a < d.program->attribute_count; ++a) {
            const auto& attr = d.program->attributes[a];
            if (attr.offset != attr.member_offset[0] || attr.offset + attr.format_bytes > v.element_bytes)
                return fail("vertex layout attribute");
        }
        if (p.depth_test != sm::CompareOp::less_equal || !r.depth_less_equal) return fail("depth test");
        if (p.depth_write != r.depth_write) return fail("depth write");
        if (p.color_write != sm::ColorWrite::rgb || !r.color_write_rgb) return fail("colour write");
        const auto cull = r.cull == nfsu::CullMode::back ? sm::Cull::back :
            r.cull == nfsu::CullMode::front ? sm::Cull::front : sm::Cull::none;
        if (p.cull != cull) return fail("cull");
        if (p.alpha_test != (r.alpha_test ? sm::CompareOp::greater : sm::CompareOp::always) ||
            (r.alpha_test && p.alpha_ref != r.alpha_ref)) return fail("alpha test");
        if (p.blend != (r.blend != nfsu::RenderBlendMode::disabled)) return fail("blend enable");
        if (p.blend && (p.blend_src != sm::BlendFactor::src_alpha || p.blend_op != sm::BlendOp::add ||
            p.blend_dst != (r.blend == nfsu::RenderBlendMode::additive ? sm::BlendFactor::one : sm::BlendFactor::one_minus_src_alpha)))
            return fail("blend factors");
        const char* variant = r.blend == nfsu::RenderBlendMode::source_over
            ? (r.alpha_test ? "alpha_tested_source_over" : "source_over")
            : r.blend == nfsu::RenderBlendMode::additive
            ? (r.alpha_test ? "alpha_tested_additive" : "additive")
            : (r.alpha_test ? "alpha_tested" : "opaque");
        if (!p.name || std::strcmp(p.name, variant)) return fail("shader variant");
        if (d.program->texture_count != (textured ? 1U : 0U)) return fail("sampler count");
        if (textured && (d.program->textures[0].binding != 0 ||
            d.program->textures[0].filter != (std::strcmp(d.program->name,
                "car_textured") == 0 ? sm::Filter::linear : sm::Filter::nearest) ||
            d.program->textures[0].wrap != sm::Wrap::repeat)) return fail("sampler state");
    }
    return {};
}
