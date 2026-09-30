#include "nfsu/draw_execution.h"
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

int main() {
    using namespace nfsu;
    namespace sm = shader_manifest;
    MeshVertex vertices[4]{};
    std::uint16_t indices[] = {0, 1, 2, 0, 1, 2};
    RecordedCommands good;
    good.width = 1280; good.height = 720; good.clear_color[3] = 255;
    UploadResource v;
    v.name = "fixture vertices"; v.kind = UploadResourceKind::vertex_buffer;
    v.data = reinterpret_cast<const unsigned char*>(vertices);
    v.bytes = 3 * sizeof(MeshVertex); v.element_bytes = sizeof(MeshVertex); v.elements = 3;
    UploadResource idx;
    idx.name = "fixture indices"; idx.kind = UploadResourceKind::index_buffer;
    idx.data = reinterpret_cast<const unsigned char*>(indices);
    idx.bytes = sizeof indices; idx.element_bytes = 2; idx.elements = 6;
    good.owning_uploads().resources = {v, idx};
    RecordedDraw d;
    d.name = "fixture draw"; d.vertex_resource = 0; d.index_resource = 1;
    d.vertex_count = 3; d.index_count = 3;
    for (const auto& p : sm::kPrograms)
        if (std::string(p.name) == "car_fallback") { d.program = &p; d.pipeline = p.variants[0]; }
    good.owning_draws().push_back(d);
    int failures = 0;
    std::string error;
    const auto check = [&](bool ok, const char* name) {
        if (!ok) { std::printf("FAIL: %s (%s)\n", name, error.c_str()); ++failures; }
    };
    check(validate_recorded_commands(good, error), "valid fixture");
    const auto refuse = [&](const char* name, const char* expected,
                            const std::function<void(RecordedCommands&)>& mutate) {
        auto c = good; mutate(c);
        check(!validate_recorded_commands(c, error) && error.find(expected) != std::string::npos, name);
    };
    refuse("zero width", "invalid viewport", [](auto& c) { c.width = 0; });
    refuse("zero height", "invalid viewport", [](auto& c) { c.height = 0; });
    refuse("width 16385", "invalid viewport", [](auto& c) { c.width = 16385; });
    refuse("height 16385", "invalid viewport", [](auto& c) { c.height = 16385; });
    auto boundary = good; boundary.width = boundary.height = 16384;
    check(validate_recorded_commands(boundary, error), "viewport 16384 accepted");
    refuse("null program", "unknown shader", [](auto& c) { c.owning_draws()[0].program = nullptr; });
    refuse("vertex resource missing", "buffer resource", [](auto& c) { c.owning_draws()[0].vertex_resource = 20; });
    refuse("index resource missing", "buffer resource", [](auto& c) { c.owning_draws()[0].index_resource = 20; });
    refuse("index range past end", "vertex or index range", [](auto& c) { c.owning_draws()[0].index_count = 9; });
    refuse("vertex count mismatch", "vertex or index range", [](auto& c) { c.owning_draws()[0].vertex_count = 2; });
    refuse("zero indices", "vertex or index range", [](auto& c) { c.owning_draws()[0].index_count = 0; });
    // Six available indices keep this inside the buffer: only the triangle
    // boundary check rejects first_index=1 (the old test masked that guard).
    refuse("first index unaligned", "vertex or index range", [](auto& c) { c.owning_draws()[0].first_index = 1; });
    refuse("index count unaligned", "vertex or index range", [](auto& c) { c.owning_draws()[0].index_count = 2; });
    refuse("base vertex nonzero", "vertex or index range", [](auto& c) { c.owning_draws()[0].first_vertex = 1; });
    boundary = good; boundary.owning_draws()[0].first_index = 3;
    check(validate_recorded_commands(boundary, error), "index range ending exactly at buffer end");
    refuse("swapped buffer kinds", "vertex or index range", [](auto& c) {
        c.owning_uploads().resources[0].kind = UploadResourceKind::index_buffer;
        c.owning_uploads().resources[1].kind = UploadResourceKind::vertex_buffer;
    });
    refuse("vertex stride mismatch", "vertex or index range", [](auto& c) {
        c.owning_uploads().resources[0].element_bytes = 40; c.owning_uploads().resources[0].bytes = 120;
    });
    refuse("index stride mismatch", "vertex or index range", [](auto& c) {
        c.owning_uploads().resources[1].element_bytes = 4; c.owning_uploads().resources[1].elements = 3;
    });
    refuse("fallback texture binding", "texture binding", [](auto& c) { c.owning_draws()[0].texture_resource = 0; });
    refuse("null payload", "invalid upload", [](auto& c) { c.owning_uploads().resources[0].data = nullptr; });
    refuse("zero upload bytes", "invalid upload", [](auto& c) {
        auto unused = c.owning_uploads().resources[0]; unused.bytes = unused.elements = 0;
        c.owning_uploads().resources.push_back(unused);
    });
    refuse("invalid resource kind", "invalid buffer", [](auto& c) {
        auto unused = c.owning_uploads().resources[0];
        unused.kind = static_cast<UploadResourceKind>(255); c.owning_uploads().resources.push_back(unused);
    });
    refuse("zero element size", "invalid buffer", [](auto& c) { c.owning_uploads().resources[0].element_bytes = 0; });
    refuse("buffer shape mismatch", "invalid buffer", [](auto& c) { c.owning_uploads().resources[0].bytes = 1; });
    refuse("oversized payload", "invalid upload", [](auto& c) { c.owning_uploads().resources[0].bytes = kMaxUploadResourceBytes + 1; });
    refuse("oversized elements", "invalid buffer", [](auto& c) { c.owning_uploads().resources[0].elements = kMaxUploadResourceBytes; });
    for (std::size_t i : {std::size_t{0}, std::size_t{2}}) {
        indices[i] = 3;
        check(!validate_recorded_commands(good, error) && error.find("missing vertex") != std::string::npos,
              i == 0 ? "first index value checked" : "last index value checked");
        indices[i] = static_cast<std::uint16_t>(i);
    }
    const auto state = [&](const char* name, const std::function<void(sm::Variant&)>& mutate) {
        refuse(name, "invalid pipeline", [&](auto& c) { mutate(c.owning_draws()[0].pipeline); });
    };
    state("alpha comparison bound", [](auto& p) { p.alpha_test = static_cast<sm::CompareOp>(255); });
    state("depth comparison bound", [](auto& p) { p.depth_test = static_cast<sm::CompareOp>(255); });
    state("blend source bound", [](auto& p) { p.blend_src = static_cast<sm::BlendFactor>(255); });
    state("blend destination bound", [](auto& p) { p.blend_dst = static_cast<sm::BlendFactor>(255); });
    state("blend operation bound", [](auto& p) { p.blend_op = static_cast<sm::BlendOp>(255); });
    state("color write bound", [](auto& p) { p.color_write = static_cast<sm::ColorWrite>(255); });
    state("cull bound", [](auto& p) { p.cull = static_cast<sm::Cull>(255); });
    state("alpha reference bound", [](auto& p) { p.alpha_ref = 256; });

    std::vector<unsigned char> pixels(512 * 512 * 4);
    const auto texture = [&](std::uint32_t w, std::uint32_t h) {
        UploadResource r; r.name = "fixture texture"; r.kind = UploadResourceKind::texture;
        r.data = pixels.data(); r.bytes = std::uint64_t{w} * h * 4;
        check(check_texture_upload_format(TextureUploadFormat::rgba8, w, h, r.bytes, r.texture, error),
              "texture fixture shape");
        return r;
    };
    auto textured = good;
    textured.owning_uploads().resources.push_back(texture(8, 2));
    for (const auto& p : sm::kPrograms)
        if (std::string(p.name) == "car_textured") {
            textured.owning_draws()[0].program = &p; textured.owning_draws()[0].pipeline = p.variants[0];
        }
    textured.owning_draws()[0].texture_resource = 2;
    check(validate_recorded_commands(textured, error), "valid textured fixture");
    const auto bad_texture = [&](const char* name, const char* expected,
                                const std::function<void(RecordedCommands&)>& mutate) {
        auto c = textured; mutate(c);
        check(!validate_recorded_commands(c, error) && error.find(expected) != std::string::npos, name);
    };
    bad_texture("texture row mismatch", "texture shape", [](auto& c) { c.owning_uploads().resources[2].texture.row_bytes = 1; });
    bad_texture("texture bytes mismatch", "texture shape", [](auto& c) { c.owning_uploads().resources[2].texture.bytes = 1; });
    bad_texture("texture binding to vertex buffer", "texture binding", [](auto& c) { c.owning_draws()[0].texture_resource = 0; });
    bad_texture("texture binding missing", "texture binding", [](auto& c) { c.owning_draws()[0].texture_resource = kNoHandle; });
    bad_texture("texture binding out of range", "texture binding", [](auto& c) { c.owning_draws()[0].texture_resource = 3; });

    TextureCopyPlan copy;
    auto r = texture(512, 512);
    check(plan_texture_copy(r, copy, error) && copy.row_pitch_bytes == 2048 &&
          copy.layer_stride_bytes == 0 && copy.staging_bytes == 1048576 && !copy.padded,
          "body copy pitch is 2048 BYTES and layer stride is zero");
    r = texture(128, 64);
    check(plan_texture_copy(r, copy, error) && copy.row_pitch_bytes == 512 &&
          copy.layer_stride_bytes == 0 && copy.staging_bytes == 32768 && !copy.padded,
          "128x64 copy pitch is 512 BYTES");
    r = texture(3, 2);
    check(plan_texture_copy(r, copy, error) && copy.row_pitch_bytes == 32 &&
          copy.layer_stride_bytes == 0 && copy.staging_bytes == 64 && copy.padded,
          "narrow copy has padded byte pitch");
    // Model the copy engine's source address calculation with visibly
    // different rows. A pitch/4 regression reads padding/the wrong row.
    std::vector<unsigned char> staged(64, 0);
    for (unsigned y = 0; y < 2; ++y) {
        std::memset(pixels.data() + y*12, 17 + y*36, 12);
        std::memcpy(staged.data() + y*32, pixels.data() + y*12, 12);
    }
    check(copy.row_pitch_bytes <= 52 &&
          std::memcmp(staged.data() + copy.row_pitch_bytes, pixels.data() + 12, 12) == 0,
          "copy engine reads second padded row intact");
    for (const auto format : {TextureUploadFormat::dxt1, TextureUploadFormat::dxt3}) {
        auto native = r;
        native.bytes = format == TextureUploadFormat::dxt1 ? 32 : 64;
        check(check_texture_upload_format(format, 5, 7, native.bytes, native.texture, error),
              "partial DXT edge blocks are accepted");
        check(plan_texture_copy(native, copy, error) && copy.row_pitch_bytes == 32 &&
              copy.staging_bytes == 64, "DXT copy counts block rows, not pixel rows");
        native.bytes -= 1;
        check(!plan_texture_copy(native, copy, error), "truncated DXT block is refused");
    }
    r.texture.row_bytes = 1;
    copy.row_pitch_bytes = 99;
    check(!plan_texture_copy(r, copy, error) && copy.row_pitch_bytes == 0, "copy refusal resets plan");
    r = texture(1, 150000000); // Size-only fixture; planner never reads pixels.
    check(!plan_texture_copy(r, copy, error) && error.find("block limit") != std::string::npos,
          "padded byte product exceeds block limit");

    DrawExecutionPlan plan;
    check(plan_draw_execution(good, plan, error) && plan.uniform_bytes == 768 &&
          plan.staging_bytes == 64 && plan.descriptor_bytes == 128 && plan.sampler_offset == 64 &&
          plan.command_bytes == 77824, "executor size arithmetic");
    auto padded = textured; padded.owning_uploads().resources[2] = texture(3, 2);
    check(plan_draw_execution(padded, plan, error) && plan.staging_bytes == 215,
          "tight plus padded staging with alignment slack");
    check(plan_draw_execution(textured, plan, error) && plan.staging_bytes == 160,
          "aligned texture needs only tight staging");
    const auto execution_refuse = [&](const char* name, const char* expected,
                                     const std::function<void(RecordedCommands&)>& mutate) {
        auto c = good; mutate(c); plan.uniform_bytes = 99;
        check(!plan_draw_execution(c, plan, error) && error.find(expected) != std::string::npos &&
              plan.uniform_bytes == 0 && plan.command_bytes == 0 && plan.staging_bytes == 0, name);
    };
    execution_refuse("nonopaque clear", "opaque", [](auto& c) { c.clear_color[3] = 19; });
    execution_refuse("executor width", "1280x720", [](auto& c) { c.width = 1279; });
    execution_refuse("executor height", "1280x720", [](auto& c) { c.height = 721; });
    execution_refuse("4097 draws", "4096", [](auto& c) { c.owning_draws().resize(4097, c.owning_draws()[0]); });
    execution_refuse("8193 resources", "8192", [](auto& c) { c.owning_uploads().resources.resize(8193, c.owning_uploads().resources[0]); });
    execution_refuse("2048 uniforms exceed budget", "transient", [](auto& c) { c.owning_draws().resize(2048, c.owning_draws()[0]); });
    auto many = good; many.owning_draws().resize(2047, many.owning_draws()[0]);
    check(plan_draw_execution(many, plan, error) && plan.uniform_bytes == 1048320,
          "2047 draws fit transient budget");
    many = good; many.owning_uploads().resources.resize(4096, many.owning_uploads().resources[0]);
    check(plan_draw_execution(many, plan, error) && plan.descriptor_bytes == 131136 &&
          plan.sampler_offset == 131072 && plan.command_bytes == 16846848,
          "4096 resources fit with separate sampler descriptor");
    check(validate_draw_update(good.owning_uploads(), good, 1, error), "same uploads can update draws");
    check(!validate_draw_update(good.owning_uploads(), good, 0, error), "update capacity refused");
    auto updated = good; updated.owning_uploads().resources[0].data = reinterpret_cast<const unsigned char*>(indices);
    check(!validate_draw_update(good.owning_uploads(), updated, 1, error), "changed borrowed payload refused");
    updated = good;
    updated.owning_uploads().resources[1].data = reinterpret_cast<const unsigned char*>(std::uintptr_t{1});
    check(!validate_draw_update(good.owning_uploads(), updated, 1, error), "changed index pointer refused before validation reads it");
    updated = good; updated.owning_uploads().resources.push_back(updated.owning_uploads().resources[0]);
    check(!validate_draw_update(good.owning_uploads(), updated, 1, error), "changed resource count refused");
    many = good; many.owning_uploads().resources.resize(8192, many.owning_uploads().resources[0]);
    check(plan_draw_execution(many, plan, error) && plan.descriptor_bytes == 262208,
          "8192 resources fit the track descriptor bound");
    // Two individually valid large textures must not overflow the staging
    // block together. The plan checks sizes without dereferencing payloads.
    many = good; many.owning_uploads().resources.push_back(texture(16384, 32768));
    many.owning_uploads().resources.push_back(many.owning_uploads().resources.back());
    check(!plan_draw_execution(many, plan, error) && error.find("staging exceeds") != std::string::npos,
          "aggregate staging block limit");
    std::printf("draw execution validation: %d failures\n", failures);
    return failures ? 1 : 0;
}
