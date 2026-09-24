// The shader manifests against what no shader carries: the C++ vertex
// structs the buffers are uploaded from, and the fixed-function state the
// CPU reference renderer gives each kind of draw. docs/SHADER_PIPELINE.md.
//
// nfsu/shader_manifest.h is generated from shaders/*.manifest by
// cmake/shader_manifest.cmake at build time; nothing here reads a manifest
// file. The GLSL check (tests/shader_source_guards.cmake) and the compiled
// shader check (the Switch build) cannot see either half of this: a vertex
// struct is not in any shader, and neither is a blend or depth state.

#include "nfsu/render.h"
#include "nfsu/shader_manifest.h"
#include "nfsu/track_render.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <utility>

namespace sm = nfsu::shader_manifest;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

const sm::Program* find_program(const std::string& name) {
    for (const sm::Program& program : sm::kPrograms) {
        if (name == program.name) {
            return &program;
        }
    }
    return nullptr;
}

bool is_car(const sm::Program& program) {
    return std::string(program.name).rfind("car_", 0) == 0;
}

bool is_textured(const sm::Program& program) {
    const std::string name = program.name;
    return name.size() > 9 && name.compare(name.size() - 9, 9, "_textured") == 0;
}

// Bytes one vertex format fetches, derived here rather than read from the
// table, so a wrong row in the generator's own format table is caught too.
std::uint32_t bytes_of(sm::VertexFormat format) {
    switch (format) {
    case sm::VertexFormat::float32x1:
        return 4;
    case sm::VertexFormat::float32x2:
        return 8;
    case sm::VertexFormat::float32x3:
        return 12;
    case sm::VertexFormat::float32x4:
        return 16;
    case sm::VertexFormat::unorm8x4:
    case sm::VertexFormat::snorm8x4:
    case sm::VertexFormat::uint8x4:
    case sm::VertexFormat::unorm16x2:
    case sm::VertexFormat::snorm16x2:
        return 4;
    }
    return 0;
}

// The draw kinds the CPU reference renderer has: the car path
// (render_scene) and the track path (render_track_draw_list), each drawing a
// group either with its resolved texture or with the fallback checker. A
// manifest for any other program describes a draw the reference does not
// make, which is a variant this project would be inventing.
void test_programs_are_the_reference_draw_kinds() {
    const std::set<std::string> expected = {
        "car_fallback", "car_textured", "track_fallback", "track_textured"};
    std::set<std::string> got;
    for (const sm::Program& program : sm::kPrograms) {
        check(got.insert(program.name).second,
              std::string("program ") + program.name + " appears once");
    }
    check(got == expected,
          "the manifests are exactly car_textured, car_fallback, "
          "track_textured and track_fallback");
}

// Each program reads one buffer, and it is the C++ struct its path holds:
// PreparedMesh::vertices, nfsu::MeshVertex, for the car path, and
// PreparedTrackMesh::vertices, nfsu::TrackVertex, for the track path. The
// stride is that struct's size, so the vertices upload as they are.
void test_buffers_are_the_vertex_structs() {
    for (const sm::Program& program : sm::kPrograms) {
        const std::string name = program.name;
        const std::string want = is_car(program) ? "nfsu::MeshVertex" : "nfsu::TrackVertex";
        const std::size_t want_size =
            is_car(program) ? sizeof(nfsu::MeshVertex) : sizeof(nfsu::TrackVertex);
        check(program.buffer_count == 1, name + ": reads one vertex buffer");
        for (std::size_t i = 0; i < program.buffer_count; ++i) {
            const sm::Buffer& buffer = program.buffers[i];
            const std::string what = name + ": buffer " + std::to_string(buffer.index);
            check(want == buffer.type_name,
                  what + " is " + want + ", not " + buffer.type_name);
            check(buffer.type_size == want_size,
                  what + ": sizeof(" + buffer.type_name + ") is " +
                      std::to_string(buffer.type_size) + ", want " +
                      std::to_string(want_size));
            check(buffer.stride == buffer.type_size,
                  what + ": stride " + std::to_string(buffer.stride) +
                      " is not sizeof(" + buffer.type_name + ") = " +
                      std::to_string(buffer.type_size));
        }
    }
}

// Each attribute reads exactly the bytes of the members it names: the first
// member starts at the attribute's offset, the others follow it with no gap,
// and together they are the bytes the format fetches. A struct that reorders,
// resizes or pads those members fails here with the member named.
void test_attributes_read_their_members() {
    for (const sm::Program& program : sm::kPrograms) {
        const std::string name = program.name;
        for (std::size_t i = 0; i < program.attribute_count; ++i) {
            const sm::Attribute& attribute = program.attributes[i];
            const std::string what = name + ": attribute " + attribute.name +
                                     " (location " +
                                     std::to_string(attribute.location) + ")";
            check(attribute.format_bytes == bytes_of(attribute.format),
                  what + ": the format fetches " +
                      std::to_string(bytes_of(attribute.format)) +
                      " bytes, the table says " +
                      std::to_string(attribute.format_bytes));
            check(attribute.components <= attribute.format_components,
                  what + ": reads more components than the format fetches");
            check(attribute.member_count >= 1 && attribute.member_count <= 4,
                  what + ": names between one and four members");
            const sm::Buffer* buffer = nullptr;
            for (std::size_t b = 0; b < program.buffer_count; ++b) {
                if (program.buffers[b].index == attribute.buffer) {
                    buffer = &program.buffers[b];
                }
            }
            check(buffer != nullptr, what + ": its buffer is declared");
            std::size_t at = attribute.offset;
            for (std::size_t m = 0; m < attribute.member_count && m < 4; ++m) {
                check(attribute.member_offset[m] == at,
                      what + ": member " + std::to_string(m) + " is at byte " +
                          std::to_string(attribute.member_offset[m]) +
                          " of the struct, want " + std::to_string(at));
                at = attribute.member_offset[m] + attribute.member_size[m];
            }
            const std::size_t covered = at - attribute.offset;
            check(covered == bytes_of(attribute.format),
                  what + ": its members hold " + std::to_string(covered) +
                      " bytes and the format fetches " +
                      std::to_string(bytes_of(attribute.format)));
            if (buffer != nullptr) {
                check(at <= buffer->stride,
                      what + ": ends past the stride of its buffer");
            }
        }
    }
}

// The reference samples a draw's resolved texture, at the nearest texel of
// the wrapped coordinate (wrap_texel), and the fallback checker samples
// nothing (Rasterizer::fill).
void test_textures_follow_the_draw_kind() {
    for (const sm::Program& program : sm::kPrograms) {
        const std::string name = program.name;
        if (!is_textured(program)) {
            check(program.texture_count == 0, name + ": samples no texture");
            continue;
        }
        check(program.texture_count == 1, name + ": samples one texture");
        if (program.texture_count != 1) {
            continue;
        }
        const sm::Texture& texture = program.textures[0];
        check(texture.stage == sm::Stage::fragment,
              name + ": the texture is sampled by the fragment stage");
        check(std::string(texture.type) == "sampler2D",
              name + ": the texture is a sampler2D");
        check(texture.filter == sm::Filter::nearest,
              name + ": the texture is sampled at the nearest texel");
        check(texture.wrap == sm::Wrap::repeat,
              name + ": the texture coordinate wraps");
    }
}

// The blend a variant's factors select, in the reference's terms, or false
// when they are not one of the reference's two blends.
bool reference_blend(const sm::Variant& variant, nfsu::RenderBlendMode& mode) {
    if (!variant.blend) {
        mode = nfsu::RenderBlendMode::disabled;
        return true;
    }
    if (variant.blend_src != sm::BlendFactor::src_alpha ||
        variant.blend_op != sm::BlendOp::add) {
        return false;
    }
    if (variant.blend_dst == sm::BlendFactor::one_minus_src_alpha) {
        mode = nfsu::RenderBlendMode::source_over;
        return true;
    }
    if (variant.blend_dst == sm::BlendFactor::one) {
        mode = nfsu::RenderBlendMode::additive;
        return true;
    }
    return false;
}

// render_scene takes a textured car draw's alpha test from texture-info +85
// and its blend from +86, through render_alpha_test_enabled and
// render_blend_mode_of, independently and only under
// MaterialStateMode::retail; the off state is the opaque pair. So the
// variants of car_textured must be exactly the pairs those two decoders can
// produce over every byte value: one variant per pair, and no variant for a
// pair they cannot produce. The alpha test is D3DCMP_GREATER against
// kRenderAlphaRef.
void test_car_textured_variants_are_the_reference_pairs() {
    std::set<std::pair<bool, int>> reference;
    for (int alpha = 0; alpha < 256; ++alpha) {
        for (int blend = 0; blend < 256; ++blend) {
            reference.insert(
                {nfsu::render_alpha_test_enabled(static_cast<std::uint8_t>(alpha)),
                 static_cast<int>(
                     nfsu::render_blend_mode_of(static_cast<std::uint8_t>(blend)))});
        }
    }
    const sm::Program* program = find_program("car_textured");
    check(program != nullptr, "car_textured has a manifest");
    if (program == nullptr) {
        return;
    }
    std::set<std::pair<bool, int>> manifest;
    for (std::size_t i = 0; i < program->variant_count; ++i) {
        const sm::Variant& variant = program->variants[i];
        const std::string what = std::string("car_textured variant ") + variant.name;
        bool alpha_test = false;
        if (variant.alpha_test == sm::CompareOp::greater) {
            alpha_test = true;
            check(variant.alpha_ref == nfsu::kRenderAlphaRef,
                  what + ": the alpha reference is " +
                      std::to_string(variant.alpha_ref) + ", the reference's is " +
                      std::to_string(nfsu::kRenderAlphaRef));
        } else {
            check(variant.alpha_test == sm::CompareOp::always,
                  what + ": the alpha test is either off or GREATER");
        }
        nfsu::RenderBlendMode mode = nfsu::RenderBlendMode::disabled;
        check(reference_blend(variant, mode),
              what + ": the blend is none, SRCALPHA/INVSRCALPHA or SRCALPHA/ONE");
        check(manifest.insert({alpha_test, static_cast<int>(mode)}).second,
              what + ": repeats another variant's state");
    }
    check(manifest == reference,
          "car_textured's variants are the " + std::to_string(reference.size()) +
              " alpha test and blend pairs the reference can produce, and it has " +
              std::to_string(manifest.size()));
}

// A car group with no resolved texture has no texture-info record and stays
// opaque in both material states (render_scene), and the track path takes no
// retail state at all (below): each of those programs is drawn in exactly
// one variant, with no alpha test and no blend.
void test_opaque_only_programs() {
    for (const char* name : {"car_fallback", "track_textured", "track_fallback"}) {
        const sm::Program* program = find_program(name);
        check(program != nullptr, std::string(name) + " has a manifest");
        if (program == nullptr) {
            continue;
        }
        check(program->variant_count == 1, std::string(name) + " has one variant");
        for (std::size_t i = 0; i < program->variant_count; ++i) {
            const sm::Variant& variant = program->variants[i];
            check(variant.alpha_test == sm::CompareOp::always && !variant.blend,
                  std::string(name) + " variant " + variant.name +
                      " has no alpha test and no blend");
        }
    }
}

// Why the track programs carry no retail variant: render_track_draw_list
// refuses MaterialStateMode::retail before it reads its scene. If that ever
// changes, this fails, and the track manifests need the retail variants.
void test_track_path_refuses_the_retail_state() {
    const nfsu::PreparedTrackScene scene;
    const nfsu::TrackDrawList list;
    nfsu::RenderOptions options;
    options.material_state = nfsu::MaterialStateMode::retail;
    const nfsu::RenderLimits limits;
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track_stats;
    std::string error;
    const bool drawn = nfsu::render_track_draw_list(
        scene, list, options, limits, frame, stats, track_stats, error);
    check(!drawn, "render_track_draw_list refuses the retail material state");
    check(error.find("retail material state") != std::string::npos,
          "and says so: " + error);
}

// The state every draw of the reference shares. A fragment is kept when its
// depth is below the stored one, or equal to it with a later triangle id,
// which in submission order is LESS_EQUAL; every drawn fragment writes depth,
// blended ones included; the frame's alpha is always 255, so no draw writes
// it; and RenderOptions culls nothing unless asked to.
void test_shared_fixed_function_state() {
    check(nfsu::RenderOptions{}.cull == nfsu::CullMode::none,
          "the reference culls nothing by default");
    for (const sm::Program& program : sm::kPrograms) {
        for (std::size_t i = 0; i < program.variant_count; ++i) {
            const sm::Variant& variant = program.variants[i];
            const std::string what =
                std::string(program.name) + " variant " + variant.name;
            check(variant.depth_test == sm::CompareOp::less_equal,
                  what + ": the depth test is LESS_EQUAL");
            check(variant.depth_write, what + ": writes depth");
            check(variant.color_write == sm::ColorWrite::rgb,
                  what + ": leaves the frame's alpha alone");
            check(variant.cull == sm::Cull::none, what + ": culls nothing");
        }
    }
}

} // namespace

int main() {
    test_programs_are_the_reference_draw_kinds();
    test_buffers_are_the_vertex_structs();
    test_attributes_read_their_members();
    test_textures_follow_the_draw_kind();
    test_car_textured_variants_are_the_reference_pairs();
    test_opaque_only_programs();
    test_track_path_refuses_the_retail_state();
    test_shared_fixed_function_state();

    if (failures != 0) {
        std::cerr << failures << " shader manifest checks failed\n";
        return 1;
    }
    std::cout << "shader manifest tests passed\n";
    return 0;
}
