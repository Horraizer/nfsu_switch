#include "nfsu/draw_commands.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace nfsu {
namespace {
namespace sm = shader_manifest;
static_assert(sizeof(DrawFrameUniform) == 112);
static_assert(sizeof(DrawInstanceUniform) == 64);
static_assert(sizeof(DrawMaterialUniform) == 80);

bool frame_state(const RenderOptions& options, const RenderLimits& limits,
                 RecordedCommands& out, std::string& error) {
    if (!options.width || !options.height || options.width > limits.max_width ||
        options.height > limits.max_height ||
        std::uint64_t{options.width} * options.height > limits.max_pixels) {
        error = "draw commands: frame dimensions exceed render limits";
        return false;
    }
    CameraFrame camera;
    if (!make_camera_frame(options.camera, options.width, options.height, camera, error)) {
        return false;
    }
    if (options.material_state == MaterialStateMode::retail &&
        !validate_material_coefficients(options.coefficients, error)) {
        return false;
    }
    const Vec3 vectors[] = {camera.eye, camera.right, camera.up, camera.forward};
    float* lanes[] = {out.frame.eye, out.frame.right, out.frame.up, out.frame.forward};
    for (std::size_t i = 0; i < 4; ++i) {
        lanes[i][0] = static_cast<float>(vectors[i].x);
        lanes[i][1] = static_cast<float>(vectors[i].y);
        lanes[i][2] = static_cast<float>(vectors[i].z);
    }
    out.frame.projection[0] = static_cast<float>(camera.focal_x);
    out.frame.projection[1] = static_cast<float>(camera.focal_y);
    out.frame.projection[2] = static_cast<float>(camera.near_plane);
    out.frame.projection[3] = static_cast<float>(camera.far_plane);
    const double length = std::sqrt(kLightX*kLightX + kLightY*kLightY + kLightZ*kLightZ);
    out.frame.light[0] = static_cast<float>(kLightX / length);
    out.frame.light[1] = static_cast<float>(kLightY / length);
    out.frame.light[2] = static_cast<float>(kLightZ / length);
    out.frame.options[0] = options.flip_v ? 1 : 0;
    out.frame.options[1] = options.vertex_color ? 1 : 0;
    out.frame.options[2] = options.shading == ShadingMode::flat ? 1 : 0;
    out.frame.options[3] = options.alpha_cutoff;
    out.width = options.width;
    out.height = options.height;
    std::copy(std::begin(options.background), std::end(options.background), out.clear_color);
    return true;
}

struct ResourceMap {
    std::vector<std::size_t> vertices;
    std::vector<std::size_t> indices;
    std::vector<std::size_t> textures;

    ResourceMap(const UploadTable& table, std::size_t meshes, std::size_t texture_count)
        : vertices(meshes, kNoHandle), indices(meshes, kNoHandle),
          textures(texture_count, kNoHandle) {
        for (std::size_t i = 0; i < table.resources.size(); ++i) {
            const auto& r = table.resources[i];
            if (r.kind == UploadResourceKind::vertex_buffer) vertices[r.source] = i;
            else if (r.kind == UploadResourceKind::index_buffer) indices[r.source] = i;
            else textures[r.source] = i;
        }
    }
};

bool pipeline(RecordedDraw& draw, bool track, const RenderTexture* texture,
              std::uint32_t flags, std::uint32_t hash, const RenderOptions& options,
              std::string& error) {
    const char* name = track ? (texture ? "track_textured" : "track_fallback")
                             : (texture ? "car_textured" : "car_fallback");
    for (const auto& p : sm::kPrograms) {
        if (std::strcmp(p.name, name) == 0) draw.program = &p;
    }
    bool alpha = false;
    RenderBlendMode blend = RenderBlendMode::disabled;
    if (!track && texture && options.material_state == MaterialStateMode::retail) {
        alpha = render_alpha_test_enabled(texture->alpha_test_mode);
        blend = render_blend_mode_of(texture->alpha_blend_mode);
        const auto gates = material_coefficient_gates(flags, options.coefficients.environment_enabled);
        const auto& c = options.coefficients;
        draw.material.alpha[0] = gates.diffuse ? static_cast<float>(c.alpha_min) : 0;
        draw.material.alpha[1] = gates.diffuse ? static_cast<float>(c.alpha_range) : 0;
        draw.material.alpha[2] = gates.diffuse ? 0.0F : 1.0F;
        if (gates.diffuse && gates.environment) {
            for (std::size_t i = 0; i < 3; ++i) {
                draw.material.environment_min[i] = static_cast<float>(c.environment_min[i]);
                draw.material.environment_range[i] = static_cast<float>(c.environment_range[i]);
                draw.material.environment_color[i] = c.environment_color[i];
            }
            draw.material.environment_color[3] = static_cast<float>(c.environment_power);
        }
    }
    const char* variant = blend == RenderBlendMode::source_over
        ? (alpha ? "alpha_tested_source_over" : "source_over")
        : blend == RenderBlendMode::additive
        ? (alpha ? "alpha_tested_additive" : "additive")
        : (alpha ? "alpha_tested" : "opaque");
    bool found = false;
    if (draw.program) {
        for (std::size_t i = 0; i < draw.program->variant_count; ++i) {
            if (std::strcmp(draw.program->variants[i].name, variant) == 0) {
                draw.pipeline = draw.program->variants[i];
                found = true;
                break;
            }
        }
    }
    if (!found) {
        error = draw.name + ": missing shader manifest variant " + name + "/" + variant;
        return false;
    }
    draw.pipeline.cull = options.cull == CullMode::back ? sm::Cull::back
        : options.cull == CullMode::front ? sm::Cull::front : sm::Cull::none;
    unsigned char color[4];
    fallback_color(hash, color);
    for (std::size_t i = 0; i < 3; ++i) draw.material.fallback[i] = color[i];
    draw.material.fallback[3] = 1;
    return true;
}

template<class Mesh, class Draw>
bool buffers(const Mesh& mesh, const Draw& source, const ResourceMap& resources,
             RecordedDraw& draw, std::string& error) {
    draw.vertex_resource = resources.vertices[source.mesh];
    draw.index_resource = resources.indices[source.mesh];
    if (draw.vertex_resource == kNoHandle || draw.index_resource == kNoHandle ||
        mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
        error = draw.name + ": missing or oversized mesh buffers";
        return false;
    }
    draw.vertex_count = static_cast<std::uint32_t>(mesh.vertices.size());
    draw.first_index = source.first_index;
    draw.index_count = source.index_count;
    for (std::size_t i = source.first_index / 3;
         i < (std::uint64_t{source.first_index} + source.index_count) / 3; ++i) {
        const auto& t = mesh.triangles[i];
        if (t.a >= mesh.vertices.size() || t.b >= mesh.vertices.size() || t.c >= mesh.vertices.size()) {
            error = draw.name + ": triangle " + std::to_string(i) + " has an invalid vertex index";
            return false;
        }
    }
    if (source.binding == BindingKind::resolved) {
        draw.texture_resource = resources.textures[source.texture];
        if (draw.texture_resource == kNoHandle) {
            error = draw.name + ": missing texture upload";
            return false;
        }
    }
    return true;
}

bool triangle_limits(std::uint64_t source, std::uint64_t submitted,
                     const RenderLimits& limits, std::string& error) {
    if (source > limits.max_source_triangles || source >= kNoTriangle ||
        submitted > limits.max_submitted_triangles || submitted >= kNoTriangle) {
        error = "draw commands: source or submitted triangles exceed render limits";
        return false;
    }
    return true;
}
} // namespace

bool record_car_commands(const PreparedCarScene& scene, const RenderOptions& options,
                         const RenderLimits& limits, std::size_t generated_texture_source,
                         RecordedCommands& commands, std::string& error) {
    commands = RecordedCommands{};
    error.clear();
    try {
        RecordedCommands out;
        if (!frame_state(options, limits, out, error) ||
            !build_car_upload_table(scene, generated_texture_source, out.uploads, error)) return false;
        std::uint64_t triangles = 0;
        for (const auto& mesh : scene.meshes) triangles += mesh.triangles.size();
        if (!triangle_limits(triangles, prepared_draw_triangles(scene), limits, error)) return false;
        const ResourceMap resources(out.uploads, scene.meshes.size(), scene.textures.size());
        PreparedMeshInstances copies;
        std::vector<std::uint32_t> order;
        if (!prepared_mesh_instances(scene, copies, error) ||
            !prepared_draw_order(scene, options.material_state == MaterialStateMode::retail,
                                 order, nullptr, error)) return false;
        for (const auto group : order) {
            const auto& source = scene.draws[group];
            if (!source.drawable || source.index_count == 0) continue;
            for (std::uint64_t c = 0; c < copies.copies(source.mesh); ++c) {
                RecordedDraw draw;
                draw.group = group;
                draw.mesh = source.mesh;
                if (copies.instanced) {
                    draw.instance = copies.order[copies.first[source.mesh] + static_cast<std::size_t>(c)];
                    std::copy(std::begin(scene.instances[draw.instance].transform),
                              std::end(scene.instances[draw.instance].transform), draw.placement.rows);
                }
                draw.name = "car group " + std::to_string(group) + " copy " +
                    (draw.instance == kNoHandle ? "authored" : std::to_string(draw.instance));
                const auto* texture = source.binding == BindingKind::resolved ? &scene.textures[source.texture] : nullptr;
                if (!buffers(scene.meshes[source.mesh], source, resources, draw, error) ||
                    !pipeline(draw, false, texture, source.flags, source.texture_hash, options, error)) return false;
                out.draws.push_back(std::move(draw));
            }
        }
        commands = std::move(out);
        return true;
    } catch (const std::bad_alloc&) {
        error = "not enough memory for car draw commands";
        return false;
    }
}

bool record_track_commands(const PreparedTrackScene& scene, const TrackDrawList& list,
                           const RenderOptions& options, const RenderLimits& limits,
                           RecordedCommands& commands, std::string& error) {
    commands = RecordedCommands{};
    error.clear();
    if (options.material_state != MaterialStateMode::off) {
        error = "the retail material state is proven for the car draw path only";
        return false;
    }
    try {
        RecordedCommands out;
        if (!frame_state(options, limits, out, error) ||
            !build_track_upload_table(scene, out.uploads, error) ||
            !validate_track_draw_list(scene, list, error)) return false;
        std::uint64_t triangles = 0;
        for (const auto& mesh : scene.region.meshes) triangles += mesh.triangles.size();
        if (!triangle_limits(triangles, list.cost.triangles, limits, error)) return false;
        const ResourceMap resources(out.uploads, scene.region.meshes.size(), scene.region.textures.size());
        for (const auto& entry : list.entries) {
            const auto& source = scene.region.draws[entry.draw];
            if (source.index_count == 0) continue;
            RecordedDraw draw;
            draw.group = entry.draw;
            draw.mesh = source.mesh;
            draw.instance = entry.instance;
            draw.name = "track group " + std::to_string(entry.draw) + " copy " + std::to_string(entry.instance);
            const auto& transform = scene.instances[entry.instance].transform;
            for (std::size_t i = 0; i < 3; ++i) {
                for (std::size_t j = 0; j < 3; ++j) draw.placement.rows[i*4+j] = transform.linear[i*3+j];
                draw.placement.rows[12+i] = transform.translation[i];
            }
            const auto* texture = source.binding == BindingKind::resolved ? &scene.region.textures[source.texture] : nullptr;
            if (!buffers(scene.region.meshes[source.mesh], source, resources, draw, error) ||
                !pipeline(draw, true, texture, 0, source.texture_hash, options, error)) return false;
            out.draws.push_back(std::move(draw));
        }
        commands = std::move(out);
        return true;
    } catch (const std::bad_alloc&) {
        error = "not enough memory for track draw commands";
        return false;
    }
}

bool reserve_draw_uniforms(const RecordedCommands& commands, MemoryArena& arena,
                           ArenaAllocation& allocation, std::string& error) {
    if (commands.draws.size() > (kMaxUploadResourceBytes / kDekoUniformBufferAlignment - 1) / 2) {
        error = "draw uniforms exceed one GPU memory block";
        return false;
    }
    const auto bytes = (1 + 2 * static_cast<std::uint64_t>(commands.draws.size())) * kDekoUniformBufferAlignment;
    const auto refusal = arena.allocate(bytes, kDekoUniformBufferAlignment, allocation);
    if (refusal != ArenaRefusal::none) {
        error = std::string("draw uniforms: ") + arena_refusal_name(refusal);
        return false;
    }
    error.clear();
    return true;
}
} // namespace nfsu
