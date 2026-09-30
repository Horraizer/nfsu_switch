// The shader manifests against what no shader carries: the C++ vertex
// structs the buffers are uploaded from, and the fixed-function state the
// CPU reference renderer gives each kind of draw. docs/SHADER_PIPELINE.md.
//
// nfsu/shader_manifest.h is generated from shaders/*.manifest by
// cmake/shader_manifest.cmake at build time; nothing here reads a manifest
// file. The GLSL check (tests/shader_source_guards.cmake) and the compiled
// shader check (the Switch build) cannot see either half of this: a vertex
// struct is not in any shader, and neither is a blend or depth state.
//
// A check that has only ever been seen to pass is not evidence (CLAUDE.md,
// Required validation), so every check of the tables also runs against
// copies of them with one thing changed, and each change must fail it with
// the finding that names it: self_checks(), at the end.

#include "nfsu/render.h"
#include "nfsu/shader_manifest.h"
#include "nfsu/track_render.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace sm = nfsu::shader_manifest;

namespace {

// Collects failed checks. A quiet reporter keeps them for a self-check to
// read instead of printing them.
class Reporter {
public:
    explicit Reporter(bool quiet = false) : quiet_(quiet) {}

    void check(bool condition, const std::string& message) {
        if (condition) {
            return;
        }
        messages_.push_back(message);
        if (!quiet_) {
            std::cerr << "FAIL: " << message << '\n';
        }
    }

    std::size_t failures() const { return messages_.size(); }
    const std::vector<std::string>& messages() const { return messages_; }

private:
    bool quiet_;
    std::vector<std::string> messages_;
};

// A copy of the generated tables that a self-check may change, with every
// program aimed at the copy's rows. The texture, buffer, attribute and
// variant rows are the ones the checks below read.
class Tables {
public:
    Tables() {
        for (const sm::Program& program : sm::kPrograms) {
            programs.push_back(program);
            buffers.emplace_back(program.buffers, program.buffers + program.buffer_count);
            attributes.emplace_back(program.attributes,
                                    program.attributes + program.attribute_count);
            textures.emplace_back(program.textures,
                                  program.textures + program.texture_count);
            variants.emplace_back(program.variants,
                                  program.variants + program.variant_count);
        }
        relink();
    }
    Tables(const Tables&) = delete;
    Tables& operator=(const Tables&) = delete;

    // Re-aims every program at this copy's rows, after a change that may
    // have added or removed one.
    void relink() {
        for (std::size_t i = 0; i < programs.size(); ++i) {
            sm::Program& program = programs[i];
            program.buffers = buffers[i].data();
            program.buffer_count = buffers[i].size();
            program.attributes = attributes[i].data();
            program.attribute_count = attributes[i].size();
            program.textures = textures[i].data();
            program.texture_count = textures[i].size();
            program.variants = variants[i].data();
            program.variant_count = variants[i].size();
        }
    }

    // The index of a program, or programs.size() when there is none.
    std::size_t index_of(const std::string& name) const {
        std::size_t i = 0;
        while (i < programs.size() && name != programs[i].name) {
            ++i;
        }
        return i;
    }

    sm::Attribute* attribute(const std::string& program, const std::string& name) {
        const std::size_t i = index_of(program);
        if (i == programs.size()) {
            return nullptr;
        }
        for (sm::Attribute& attribute : attributes[i]) {
            if (name == attribute.name) {
                return &attribute;
            }
        }
        return nullptr;
    }

    sm::Variant* variant(const std::string& program, const std::string& name) {
        const std::size_t i = index_of(program);
        if (i == programs.size()) {
            return nullptr;
        }
        for (sm::Variant& variant : variants[i]) {
            if (name == variant.name) {
                return &variant;
            }
        }
        return nullptr;
    }

    std::vector<sm::Program> programs;
    std::vector<std::vector<sm::Buffer>> buffers;
    std::vector<std::vector<sm::Attribute>> attributes;
    std::vector<std::vector<sm::Texture>> textures;
    std::vector<std::vector<sm::Variant>> variants;
};

using Programs = std::vector<sm::Program>;

const sm::Program* find_program(const Programs& programs, const std::string& name) {
    for (const sm::Program& program : programs) {
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

// The element a format reads from memory: float32 reads 4-byte floats,
// unorm8 and uint8 unsigned bytes, snorm8 signed bytes, and the 16-bit
// formats 2-byte integers of their sign.
std::pair<sm::Element, std::size_t> format_element(sm::VertexFormat format) {
    switch (format) {
    case sm::VertexFormat::float32x1:
    case sm::VertexFormat::float32x2:
    case sm::VertexFormat::float32x3:
    case sm::VertexFormat::float32x4:
        return {sm::Element::floating, 4};
    case sm::VertexFormat::unorm8x4:
    case sm::VertexFormat::uint8x4:
        return {sm::Element::unsigned_integer, 1};
    case sm::VertexFormat::snorm8x4:
        return {sm::Element::signed_integer, 1};
    case sm::VertexFormat::unorm16x2:
        return {sm::Element::unsigned_integer, 2};
    case sm::VertexFormat::snorm16x2:
        return {sm::Element::signed_integer, 2};
    }
    return {sm::Element::other, 0};
}

const char* element_name(sm::Element element) {
    switch (element) {
    case sm::Element::none:
        return "nothing";
    case sm::Element::floating:
        return "floating-point";
    case sm::Element::signed_integer:
        return "signed integer";
    case sm::Element::unsigned_integer:
        return "unsigned integer";
    case sm::Element::other:
        return "neither a number nor an array of numbers";
    }
    return "unknown";
}

// The draw kinds the CPU reference renderer has: the car path
// (render_scene) and the track path (render_track_draw_list), each drawing a
// group either with its resolved texture or with the fallback checker. A
// manifest for any other program describes a draw the reference does not
// make, which is a variant this project would be inventing.
void test_programs_are_the_reference_draw_kinds(const Programs& programs, Reporter& r) {
    const std::set<std::string> expected = {
        "car_fallback", "car_textured", "sky_textured", "track_fallback", "track_textured"};
    std::set<std::string> got;
    for (const sm::Program& program : programs) {
        r.check(got.insert(program.name).second,
                std::string("program ") + program.name + " appears once");
    }
    r.check(got == expected,
            "the manifests are exactly car_textured, car_fallback, "
            "track_textured and track_fallback");
}

// Each program reads one buffer, and it is the C++ struct its path holds:
// PreparedMesh::vertices, nfsu::MeshVertex, for the car path, and
// PreparedTrackMesh::vertices, nfsu::TrackVertex, for the track path. The
// stride is that struct's size, so the vertices upload as they are.
void test_buffers_are_the_vertex_structs(const Programs& programs, Reporter& r) {
    for (const sm::Program& program : programs) {
        const std::string name = program.name;
        const std::string want = is_car(program) ? "nfsu::MeshVertex" : "nfsu::TrackVertex";
        const std::size_t want_size =
            is_car(program) ? sizeof(nfsu::MeshVertex) : sizeof(nfsu::TrackVertex);
        r.check(program.buffer_count == 1, name + ": reads one vertex buffer");
        for (std::size_t i = 0; i < program.buffer_count; ++i) {
            const sm::Buffer& buffer = program.buffers[i];
            const std::string what = name + ": buffer " + std::to_string(buffer.index);
            r.check(want == buffer.type_name,
                    what + " is " + want + ", not " + buffer.type_name);
            r.check(buffer.type_size == want_size,
                    what + ": sizeof(" + buffer.type_name + ") is " +
                        std::to_string(buffer.type_size) + ", want " +
                        std::to_string(want_size));
            r.check(buffer.stride == buffer.type_size,
                    what + ": stride " + std::to_string(buffer.stride) +
                        " is not sizeof(" + buffer.type_name + ") = " +
                        std::to_string(buffer.type_size));
        }
    }
}

// Each attribute reads exactly the bytes of the members it names: the first
// member starts at the attribute's offset, the others follow it with no gap,
// together they are the bytes the format fetches, and each is made of the
// elements the format reads - four-byte floats for float32, bytes for the
// 8-bit formats. A struct that reorders, resizes, pads or retypes those
// members fails here with the member named.
void test_attributes_read_their_members(const Programs& programs, Reporter& r) {
    for (const sm::Program& program : programs) {
        const std::string name = program.name;
        for (std::size_t i = 0; i < program.attribute_count; ++i) {
            const sm::Attribute& attribute = program.attributes[i];
            const std::string what = name + ": attribute " + attribute.name +
                                     " (location " +
                                     std::to_string(attribute.location) + ")";
            r.check(attribute.format_bytes == bytes_of(attribute.format),
                    what + ": the format fetches " +
                        std::to_string(bytes_of(attribute.format)) +
                        " bytes, the table says " +
                        std::to_string(attribute.format_bytes));
            r.check(attribute.components <= attribute.format_components,
                    what + ": reads more components than the format fetches");
            r.check(attribute.member_count >= 1 && attribute.member_count <= 4,
                    what + ": names between one and four members");
            const sm::Buffer* buffer = nullptr;
            for (std::size_t b = 0; b < program.buffer_count; ++b) {
                if (program.buffers[b].index == attribute.buffer) {
                    buffer = &program.buffers[b];
                }
            }
            r.check(buffer != nullptr, what + ": its buffer is declared");
            const std::pair<sm::Element, std::size_t> element =
                format_element(attribute.format);
            std::size_t at = attribute.offset;
            for (std::size_t m = 0; m < attribute.member_count && m < 4; ++m) {
                const std::string member = what + ": member " + std::to_string(m);
                r.check(attribute.member_offset[m] == at,
                        member + " is at byte " +
                            std::to_string(attribute.member_offset[m]) +
                            " of the struct, want " + std::to_string(at));
                r.check(attribute.member_element[m] == element.first,
                        member + " is " + element_name(attribute.member_element[m]) +
                            ", and the format reads " + element_name(element.first) +
                            " elements");
                r.check(attribute.member_element_size[m] == element.second,
                        member + " has " +
                            std::to_string(attribute.member_element_size[m]) +
                            "-byte elements, and the format reads " +
                            std::to_string(element.second) + "-byte ones");
                at = attribute.member_offset[m] + attribute.member_size[m];
            }
            const std::size_t covered = at - attribute.offset;
            r.check(covered == bytes_of(attribute.format),
                    what + ": its members hold " + std::to_string(covered) +
                        " bytes and the format fetches " +
                        std::to_string(bytes_of(attribute.format)));
            if (buffer != nullptr) {
                r.check(at <= buffer->stride,
                        what + ": ends past the stride of its buffer");
            }
        }
    }
}

// The car effect samples linearly and the track reference samples nearest;
// both wrap their coordinates. The fallback checker samples nothing.
void test_textures_follow_the_draw_kind(const Programs& programs, Reporter& r) {
    for (const sm::Program& program : programs) {
        const std::string name = program.name;
        if (!is_textured(program)) {
            r.check(program.texture_count == 0, name + ": samples no texture");
            continue;
        }
        r.check(program.texture_count == 1, name + ": samples one texture");
        if (program.texture_count != 1) {
            continue;
        }
        const sm::Texture& texture = program.textures[0];
        r.check(texture.stage == sm::Stage::fragment,
                name + ": the texture is sampled by the fragment stage");
        r.check(std::string(texture.type) == "sampler2D",
                name + ": the texture is a sampler2D");
        r.check(texture.filter == (name == "car_textured"
                   ? sm::Filter::linear : sm::Filter::nearest),
                name + ": the texture filter matches its effect");
        r.check(texture.wrap == sm::Wrap::repeat,
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

// The name a variant of that state has, which is how a backend picks one:
// alpha_tested when the alpha test is on, then the blend, source_over or
// additive, joined by an underscore - or opaque when neither is on.
// docs/SHADER_PIPELINE.md lists them.
std::string variant_name(bool alpha_test, nfsu::RenderBlendMode mode) {
    std::string name = alpha_test ? "alpha_tested" : "";
    const char* blend = nullptr;
    if (mode == nfsu::RenderBlendMode::source_over) {
        blend = "source_over";
    } else if (mode == nfsu::RenderBlendMode::additive) {
        blend = "additive";
    }
    if (blend != nullptr) {
        name += name.empty() ? std::string(blend) : std::string("_") + blend;
    }
    return name.empty() ? "opaque" : name;
}

// render_scene takes a textured car draw's alpha test from texture-info +85
// and its blend from +86, through render_alpha_test_enabled and
// render_blend_mode_of, independently and only under
// MaterialStateMode::retail; the off state is the opaque pair. So the
// variants of car_textured must be exactly the pairs those two decoders can
// produce over every byte value: one variant per pair, named for it, and no
// variant for a pair they cannot produce. The alpha test is D3DCMP_GREATER
// against kRenderAlphaRef.
void test_car_textured_variants_are_the_reference_pairs(const Programs& programs,
                                                         Reporter& r) {
    std::set<std::pair<bool, int>> reference;
    for (int alpha = 0; alpha < 256; ++alpha) {
        for (int blend = 0; blend < 256; ++blend) {
            reference.insert(
                {nfsu::render_alpha_test_enabled(static_cast<std::uint8_t>(alpha)),
                 static_cast<int>(
                     nfsu::render_blend_mode_of(static_cast<std::uint8_t>(blend)))});
        }
    }
    const sm::Program* program = find_program(programs, "car_textured");
    r.check(program != nullptr, "car_textured has a manifest");
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
            r.check(variant.alpha_ref == nfsu::kRenderAlphaRef,
                    what + ": the alpha reference is " +
                        std::to_string(variant.alpha_ref) + ", the reference's is " +
                        std::to_string(nfsu::kRenderAlphaRef));
        } else {
            r.check(variant.alpha_test == sm::CompareOp::always,
                    what + ": the alpha test is either off or GREATER");
        }
        nfsu::RenderBlendMode mode = nfsu::RenderBlendMode::disabled;
        const bool known = reference_blend(variant, mode);
        r.check(known,
                what + ": the blend is none, SRCALPHA/INVSRCALPHA or SRCALPHA/ONE");
        if (known) {
            const std::string want = variant_name(alpha_test, mode);
            r.check(want == variant.name,
                    what + ": its state is the one named " + want);
        }
        r.check(manifest.insert({alpha_test, static_cast<int>(mode)}).second,
                what + ": repeats another variant's state");
    }
    r.check(manifest == reference,
            "car_textured's variants are the " + std::to_string(reference.size()) +
                " alpha test and blend pairs the reference can produce, and it has " +
                std::to_string(manifest.size()));
}

// A group with no resolved texture has no texture-info record and stays
// opaque in both material states. Only the fallback programs are opaque-only.
void test_opaque_only_programs(const Programs& programs, Reporter& r) {
    for (const char* name : {"car_fallback", "track_fallback"}) {
        const sm::Program* program = find_program(programs, name);
        r.check(program != nullptr, std::string(name) + " has a manifest");
        if (program == nullptr) {
            continue;
        }
        r.check(program->variant_count == 1, std::string(name) + " has one variant");
        for (std::size_t i = 0; i < program->variant_count; ++i) {
            const sm::Variant& variant = program->variants[i];
            const std::string what = std::string(name) + " variant " + variant.name;
            r.check(std::string(variant.name) == "opaque", what + " is named opaque");
            r.check(variant.alpha_test == sm::CompareOp::always && !variant.blend,
                    what + " has no alpha test and no blend");
        }
    }
}

void test_track_textured_variants_are_the_reference_pairs(const Programs& programs,
                                                           Reporter& r) {
    const sm::Program* program = find_program(programs, "track_textured");
    r.check(program != nullptr, "track_textured has a manifest");
    if (program == nullptr) return;
    std::set<std::pair<bool, int>> pairs;
    for (std::size_t i = 0; i < program->variant_count; ++i) {
        const sm::Variant& variant = program->variants[i];
        const bool alpha_test = variant.alpha_test == sm::CompareOp::greater;
        const std::string what = std::string("track_textured variant ") + variant.name;
        r.check(variant.alpha_test == sm::CompareOp::always || alpha_test,
                what + ": alpha test is off or GREATER");
        if (alpha_test) {
            r.check(variant.alpha_ref == nfsu::kRenderAlphaRef,
                    what + ": alpha reference matches the CPU renderer");
        }
        nfsu::RenderBlendMode mode = nfsu::RenderBlendMode::disabled;
        const bool known = reference_blend(variant, mode);
        r.check(known, what + ": blend mode is supported");
        if (known) {
            r.check(std::string(variant.name) == variant_name(alpha_test, mode),
                    what + ": name matches state");
        }
        r.check(pairs.insert({alpha_test, static_cast<int>(mode)}).second,
                what + ": state is unique");
        r.check(variant.depth_write == (mode == nfsu::RenderBlendMode::disabled),
                what + ": world blend disables depth writes");
    }
    r.check(pairs.size() == 6, "track_textured has all six alpha and blend pairs");
}

// The state every draw of the reference shares. A fragment is kept when its
// depth is below the stored one, or equal to it with a later triangle id,
// which in submission order is LESS_EQUAL; world blends disable depth writes;
// the frame's alpha is always 255, so no draw writes
// it; and RenderOptions culls nothing unless asked to.
//
// Unlike test_car_textured_variants_are_the_reference_pairs above, these
// four checks are against the constants below, not against src/render.cpp:
// nothing here calls render_scene or render_track_draw_list. A change to the
// reference's depth test, its depth write, its alpha write or
// RenderOptions::cull's default that is not also made by hand to these four
// constants (and to docs/SHADER_PIPELINE.md's "C++" section) drifts unseen
// by this test; only nfsu_render_tests and nfsu_track_render_tests, which
// exercise the reference directly, would still fail (review comment 2856,
// F2).
void test_shared_fixed_function_state(const Programs& programs, Reporter& r) {
    r.check(nfsu::RenderOptions{}.cull == nfsu::CullMode::none,
            "the reference culls nothing by default");
    for (const sm::Program& program : programs) {
        for (std::size_t i = 0; i < program.variant_count; ++i) {
            const sm::Variant& variant = program.variants[i];
            const std::string what =
                std::string(program.name) + " variant " + variant.name;
            r.check(variant.depth_test == sm::CompareOp::less_equal,
                    what + ": the depth test is LESS_EQUAL");
            const bool world_blend = (std::string(program.name) == "track_textured" && variant.blend) ||
                                     std::string(program.name) == "sky_textured";
            r.check(variant.depth_write == !world_blend,
                    what + (world_blend ? ": leaves depth alone" : ": writes depth"));
            r.check(variant.color_write == sm::ColorWrite::rgb,
                    what + ": leaves the frame's alpha alone");
            r.check(variant.cull == sm::Cull::none, what + ": culls nothing");
        }
    }
}

// Every check of the tables.
void check_tables(const Programs& programs, Reporter& r) {
    test_programs_are_the_reference_draw_kinds(programs, r);
    test_buffers_are_the_vertex_structs(programs, r);
    test_attributes_read_their_members(programs, r);
    test_textures_follow_the_draw_kind(programs, r);
    test_car_textured_variants_are_the_reference_pairs(programs, r);
    test_track_textured_variants_are_the_reference_pairs(programs, r);
    test_opaque_only_programs(programs, r);
    test_shared_fixed_function_state(programs, r);
}

void test_track_path_accepts_the_retail_state(Reporter& r) {
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
    r.check(drawn, "render_track_draw_list accepts the retail material state: " + error);
}

// One change to a copy of the tables, which the checks must fail with a
// finding containing `expected`. `change` returns false when what it
// changes is no longer in the tables, which is reported as stale.
struct SelfCheck {
    const char* label;
    std::function<bool(Tables&)> change;
    const char* expected;
};

// The changes someone editing a manifest, or a vertex struct, and not the
// reference would make - one per kind of finding the checks above report.
std::vector<SelfCheck> self_check_cases() {
    return {
        {"a program the reference does not draw",
         [](Tables& t) {
             const std::size_t i = t.index_of("car_fallback");
             if (i == t.programs.size()) {
                 return false;
             }
             t.programs[i].name = "car_decal";
             return true;
         },
         "the manifests are exactly car_textured, car_fallback, track_textured and "
         "track_fallback"},
        {"a car buffer of the track's struct",
         [](Tables& t) {
             const std::size_t i = t.index_of("car_textured");
             if (i == t.programs.size() || t.buffers[i].empty()) {
                 return false;
             }
             t.buffers[i][0].type_name = "nfsu::TrackVertex";
             return true;
         },
         "car_textured: buffer 0 is nfsu::MeshVertex, not nfsu::TrackVertex"},
        {"a stride that is not the struct's size",
         [](Tables& t) {
             const std::size_t i = t.index_of("track_fallback");
             if (i == t.programs.size() || t.buffers[i].empty()) {
                 return false;
             }
             t.buffers[i][0].stride += 4;
             return true;
         },
         "track_fallback: buffer 0: stride 28 is not sizeof(nfsu::TrackVertex)"},
        {"an attribute offset that is not its first member's",
         [](Tables& t) {
             sm::Attribute* a = t.attribute("car_textured", "texcoord");
             if (a == nullptr) {
                 return false;
             }
             a->offset -= 4;
             return true;
         },
         "car_textured: attribute texcoord (location 2): member 0 is at byte 28 of the "
         "struct, want 24"},
        {"a member retyped from float to a same-size integer",
         [](Tables& t) {
             sm::Attribute* a = t.attribute("track_textured", "texcoord");
             if (a == nullptr) {
                 return false;
             }
             a->member_element[1] = sm::Element::signed_integer;
             return true;
         },
         "track_textured: attribute texcoord (location 2): member 1 is signed integer, "
         "and the format reads floating-point elements"},
        {"a byte member widened to two-byte elements",
         [](Tables& t) {
             sm::Attribute* a = t.attribute("car_fallback", "color");
             if (a == nullptr) {
                 return false;
             }
             a->member_element_size[0] = 2;
             return true;
         },
         "car_fallback: attribute color (location 1): member 0 has 2-byte elements, and "
         "the format reads 1-byte ones"},
        {"a texture sampled with a linear filter",
         [](Tables& t) {
             const std::size_t i = t.index_of("track_textured");
             if (i == t.programs.size() || t.textures[i].empty()) {
                 return false;
             }
             t.textures[i][0].filter = sm::Filter::linear;
             return true;
         },
         "track_textured: the texture filter matches its effect"},
        {"the blends of two variants swapped under their names",
         [](Tables& t) {
             sm::Variant* over = t.variant("car_textured", "source_over");
             sm::Variant* add = t.variant("car_textured", "additive");
             if (over == nullptr || add == nullptr) {
                 return false;
             }
             std::swap(over->blend_dst, add->blend_dst);
             return true;
         },
         "car_textured variant source_over: its state is the one named additive"},
        {"a reference pair with no variant",
         [](Tables& t) {
             const std::size_t i = t.index_of("car_textured");
             if (i == t.programs.size() || t.variants[i].empty()) {
                 return false;
             }
             t.variants[i].pop_back();
             t.relink();
             return true;
         },
         "car_textured's variants are the 6 alpha test and blend pairs the reference can "
         "produce, and it has 5"},
        {"another alpha reference",
         [](Tables& t) {
             sm::Variant* v = t.variant("car_textured", "alpha_tested");
             if (v == nullptr) {
                 return false;
             }
             v->alpha_ref += 1;
             return true;
         },
         "car_textured variant alpha_tested: the alpha reference is 171, the "
         "reference's is 170"},
        {"an opaque-only program's variant renamed",
         [](Tables& t) {
             sm::Variant* v = t.variant("track_fallback", "opaque");
             if (v == nullptr) {
                 return false;
             }
             v->name = "solid";
             return true;
         },
         "track_fallback variant solid is named opaque"},
        {"an opaque-only program blended",
         [](Tables& t) {
             sm::Variant* v = t.variant("car_fallback", "opaque");
             if (v == nullptr) {
                 return false;
             }
             v->blend = true;
             return true;
         },
         "car_fallback variant opaque has no alpha test and no blend"},
        {"another depth test",
         [](Tables& t) {
             sm::Variant* v = t.variant("track_textured", "opaque");
             if (v == nullptr) {
                 return false;
             }
             v->depth_test = sm::CompareOp::less;
             return true;
         },
         "track_textured variant opaque: the depth test is LESS_EQUAL"},
        {"a variant that writes alpha",
         [](Tables& t) {
             sm::Variant* v = t.variant("car_textured", "additive");
             if (v == nullptr) {
                 return false;
             }
             v->color_write = sm::ColorWrite::rgba;
             return true;
         },
         "car_textured variant additive: leaves the frame's alpha alone"},
        {"a variant that culls",
         [](Tables& t) {
             sm::Variant* v = t.variant("car_textured", "opaque");
             if (v == nullptr) {
                 return false;
             }
             v->cull = sm::Cull::back;
             return true;
         },
         "car_textured variant opaque: culls nothing"},
    };
}

// Runs every self-check and reports, into `r`, each one that did not fail
// the checks with its expected finding. Classified as the guard scripts
// classify their cases: caught, missed (no failure at all), wrong (failures,
// but not that one) or stale. Returns the number caught.
std::size_t self_checks(Reporter& r) {
    std::size_t caught = 0;
    for (const SelfCheck& self_check : self_check_cases()) {
        Tables tables;
        const std::string label = std::string("self-check \"") + self_check.label + "\"";
        if (!self_check.change(tables)) {
            r.check(false, label + " is stale: what it changes is not in the tables");
            continue;
        }
        Reporter quiet(true);
        check_tables(tables.programs, quiet);
        if (quiet.failures() == 0) {
            r.check(false, label + " was missed: the checks pass with it");
            continue;
        }
        bool found = false;
        for (const std::string& message : quiet.messages()) {
            found = found || message.find(self_check.expected) != std::string::npos;
        }
        std::string reported;
        for (const std::string& message : quiet.messages()) {
            reported += "\n    " + message;
        }
        r.check(found, label + " failed the checks, but not with \"" +
                           self_check.expected + "\":" + reported);
        if (found) {
            ++caught;
        }
    }
    return caught;
}

} // namespace

int main() {
    Reporter reporter;
    const Tables committed;
    check_tables(committed.programs, reporter);
    test_track_path_accepts_the_retail_state(reporter);
    const std::size_t caught = self_checks(reporter);

    if (reporter.failures() != 0) {
        std::cerr << reporter.failures() << " shader manifest checks failed\n";
        return 1;
    }
    std::cout << "shader manifest tests passed, and " << caught
              << " changed copies of the tables were each caught\n";
    return 0;
}
