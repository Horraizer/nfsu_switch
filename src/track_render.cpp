#include "nfsu/track_render.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <string>
#include <vector>

// The same guarantee src/render.cpp asserts, for the same reason: with
// FLT_EVAL_METHOD != 0 an intermediate could be evaluated wider than double
// and two builds would disagree on a boundary. The framing arithmetic and
// the placement below are both boundary-sensitive - the first decides
// whether a vertex lands inside the frame, the second where.
#include <cfloat>
static_assert(FLT_EVAL_METHOD == 0, "track renderer requires FLT_EVAL_METHOD == 0");

namespace nfsu {

namespace {

bool finite_value(double value) noexcept {
    return value == value && value != std::numeric_limits<double>::infinity() &&
           value != -std::numeric_limits<double>::infinity();
}

void extend_bounds(TrackWorldBounds& bounds, const Vec3& point) noexcept {
    const double v[3] = {point.x, point.y, point.z};
    if (!bounds.valid) {
        for (int k = 0; k < 3; ++k) {
            bounds.min[k] = v[k];
            bounds.max[k] = v[k];
        }
        bounds.valid = true;
    } else {
        for (int k = 0; k < 3; ++k) {
            bounds.min[k] = std::min(bounds.min[k], v[k]);
            bounds.max[k] = std::max(bounds.max[k], v[k]);
        }
    }
    ++bounds.points;
}

// A box stated in the raw slot order, in the world frame. The map negates
// slot 1, so the world z interval is the negated raw interval with its ends
// exchanged; writing it the obvious way would produce min > max and a box
// that contains nothing.
void extend_bounds_from_raw_box(
    TrackWorldBounds& bounds,
    const float raw_min[3],
    const float raw_max[3]) noexcept {
    const double lo[3] = {raw_min[0], raw_min[1], raw_min[2]};
    const double hi[3] = {raw_max[0], raw_max[1], raw_max[2]};
    // The two world-frame corners the map sends the raw corners to. Every
    // other corner of the box lies between them on each axis.
    const Vec3 a{lo[0], lo[2], -hi[1]};
    const Vec3 b{hi[0], hi[2], -lo[1]};
    extend_bounds(bounds, a);
    extend_bounds(bounds, b);
}

// The sine of the half-angle whose tangent is t, for t > 0, without a libm
// transcendental: sin = t / sqrt(1 + t^2).
double sine_of_half_angle(double t) noexcept {
    return t / det_sqrt(1.0 + t * t);
}

} // namespace

Vec3 track_placed_vertex(
    const TrackVertex& vertex,
    const TrackTransform& transform) noexcept {
    // The raw slot order of a TrackVertex is (px, pz, py); see
    // include/nfsu/track.h. TrackTransform::linear is row-major over that
    // order, applied to the vertex as a row vector, and translation is the
    // row a 4x4 would carry as row 3.
    const double slot[3] = {vertex.px, vertex.pz, vertex.py};
    double moved[3];
    for (int k = 0; k < 3; ++k) {
        moved[k] = slot[0] * static_cast<double>(transform.linear[k]) +
                   slot[1] * static_cast<double>(transform.linear[3 + k]) +
                   slot[2] * static_cast<double>(transform.linear[6 + k]) +
                   static_cast<double>(transform.translation[k]);
    }
    // docs/FORMATS.md:286, the same map track_world_from_raw applies:
    // x = px, y = py, z = -pz, over a raw triple (px, pz, py).
    return {moved[0], moved[2], -moved[1]};
}

bool track_draw_list_bounds(
    const PreparedTrackScene& scene,
    const TrackDrawList& list,
    TrackDrawListBounds& bounds,
    std::string& error) {
    bounds = TrackDrawListBounds{};
    std::vector<unsigned char> seen;
    std::vector<unsigned char> has_vertex;
    try {
        seen.assign(scene.instances.size(), 0);
        has_vertex.assign(scene.instances.size(), 0);
    } catch (const std::bad_alloc&) {
        error = "not enough memory for the instance table";
        return false;
    }
    for (std::size_t e = 0; e < list.entries.size(); ++e) {
        const TrackDrawListEntry& entry = list.entries[e];
        if (entry.instance >= scene.instances.size()) {
            error = "draw list entry " + std::to_string(e) + " names instance " +
                    std::to_string(entry.instance) + "; the scene holds " +
                    std::to_string(scene.instances.size());
            return false;
        }
        if (entry.draw >= scene.region.draws.size()) {
            error = "draw list entry " + std::to_string(e) + " names group " +
                    std::to_string(entry.draw) + "; the region holds " +
                    std::to_string(scene.region.draws.size());
            return false;
        }
        const PreparedTrackInstance& instance = scene.instances[entry.instance];
        const TrackDraw& draw = scene.region.draws[entry.draw];
        if (draw.mesh != instance.mesh || draw.mesh >= scene.region.meshes.size()) {
            error = "draw list entry " + std::to_string(e) +
                    " names a group of mesh " + std::to_string(draw.mesh) +
                    " and an instance of mesh " + std::to_string(instance.mesh);
            return false;
        }
        const PreparedTrackMesh& mesh = scene.region.meshes[draw.mesh];
        if (seen[entry.instance] == 0) {
            seen[entry.instance] = 1;
            ++bounds.instances;
            extend_bounds_from_raw_box(bounds.stored, instance.bounds_min,
                                       instance.bounds_max);
        }
        const std::uint64_t first = draw.first_index;
        const std::uint64_t count = draw.index_count;
        for (std::uint64_t t = first / 3; t < (first + count) / 3; ++t) {
            if (t >= mesh.triangles.size()) {
                error = "draw list entry " + std::to_string(e) +
                        " reaches triangle " + std::to_string(t) + " of a mesh "
                        "with " + std::to_string(mesh.triangles.size());
                return false;
            }
            const MeshTriangle& triangle = mesh.triangles[static_cast<std::size_t>(t)];
            const std::uint16_t indices[3] = {triangle.a, triangle.b, triangle.c};
            // A triangle the renderer will reject as a bad index contributes
            // nothing there, so it contributes nothing here either: the two
            // walks have to visit the same vertices or the frame could hold a
            // point the bounds do not.
            if (indices[0] >= mesh.vertices.size() ||
                indices[1] >= mesh.vertices.size() ||
                indices[2] >= mesh.vertices.size()) {
                continue;
            }
            for (int k = 0; k < 3; ++k) {
                extend_bounds(bounds.drawn,
                              track_placed_vertex(mesh.vertices[indices[k]],
                                                  instance.transform));
            }
            has_vertex[entry.instance] = 1;
        }
    }
    for (std::size_t i = 0; i < seen.size(); ++i) {
        if (seen[i] != 0 && has_vertex[i] == 0) {
            ++bounds.instances_without_vertices;
        }
    }
    return true;
}

bool frame_track_bounds(
    const TrackWorldBounds& bounds,
    std::uint32_t width,
    std::uint32_t height,
    const TrackFramingOptions& options,
    OrbitCamera& camera,
    std::string& error) {
    if (!bounds.valid) {
        error = "the bounds hold no point, so there is nothing to frame";
        return false;
    }
    if (width == 0 || height == 0) {
        error = "frame dimensions must be positive";
        return false;
    }
    for (int k = 0; k < 3; ++k) {
        if (!finite_value(bounds.min[k]) || !finite_value(bounds.max[k])) {
            error = "the bounds are not finite on axis " + std::to_string(k);
            return false;
        }
        if (bounds.min[k] > bounds.max[k]) {
            error = "the bounds are inverted on axis " + std::to_string(k);
            return false;
        }
    }
    if (!finite_value(options.yaw_deg) || !finite_value(options.pitch_deg) ||
        !finite_value(options.fov_y_deg) || !finite_value(options.margin)) {
        error = "every framing option must be finite";
        return false;
    }
    if (options.yaw_deg < -kMaxYawMagnitude || options.yaw_deg > kMaxYawMagnitude) {
        error = "framing yaw must be in -" + std::to_string(kMaxYawMagnitude) +
                ".." + std::to_string(kMaxYawMagnitude);
        return false;
    }
    if (options.pitch_deg < kMinPitch || options.pitch_deg > kMaxPitch) {
        error = "framing pitch must be in " + std::to_string(kMinPitch) + ".." +
                std::to_string(kMaxPitch);
        return false;
    }
    if (options.fov_y_deg < kMinFov || options.fov_y_deg > kMaxFov) {
        error = "framing field of view must be in " + std::to_string(kMinFov) +
                ".." + std::to_string(kMaxFov);
        return false;
    }
    if (options.margin < kMinTrackFramingMargin ||
        options.margin > kMaxTrackFramingMargin) {
        error = "framing margin must be in " +
                std::to_string(kMinTrackFramingMargin) + ".." +
                std::to_string(kMaxTrackFramingMargin);
        return false;
    }

    double centre[3];
    double radius_squared = 0.0;
    for (int k = 0; k < 3; ++k) {
        centre[k] = (bounds.min[k] + bounds.max[k]) * 0.5;
        const double half = (bounds.max[k] - bounds.min[k]) * 0.5;
        radius_squared += half * half;
    }
    const double radius = det_sqrt(radius_squared);
    if (!finite_value(radius) || !finite_value(centre[0]) ||
        !finite_value(centre[1]) || !finite_value(centre[2])) {
        error = "the bounds are too large to frame";
        return false;
    }
    for (int k = 0; k < 3; ++k) {
        if (centre[k] < -kMaxTargetMagnitude || centre[k] > kMaxTargetMagnitude) {
            error = "the bounds centre is at " + std::to_string(centre[k]) +
                    " on axis " + std::to_string(k) +
                    ", outside the camera target range of +-" +
                    std::to_string(kMaxTargetMagnitude);
            return false;
        }
    }

    const double half_fov = options.fov_y_deg * 0.5;
    const double cos_half = det_cos_deg(half_fov);
    const double sin_half = det_sin_deg(half_fov);
    if (!(cos_half > 0.0) || !(sin_half > 0.0)) {
        error = "the field of view has no usable half-angle";
        return false;
    }
    const double tan_y = sin_half / cos_half;
    const double tan_x = tan_y * static_cast<double>(width) /
                         static_cast<double>(height);
    const double sine = std::min(sine_of_half_angle(tan_x),
                                 sine_of_half_angle(tan_y));
    if (!(sine > 0.0) || !finite_value(sine)) {
        error = "the field of view admits no half-extent";
        return false;
    }
    double distance = radius / sine * options.margin;
    if (!finite_value(distance)) {
        error = "the bounds are too large to frame";
        return false;
    }
    if (distance > kMaxDistance) {
        error = "framing the bounds needs a camera distance of " +
                std::to_string(distance) + ", above the limit of " +
                std::to_string(kMaxDistance);
        return false;
    }
    if (distance < kMinDistance) {
        distance = kMinDistance;
    }

    double near_plane = (distance - radius) * 0.5;
    near_plane = std::clamp(near_plane, kMinNear, kMaxNear);
    double far_plane = (distance + radius) * 1.25;
    if (!finite_value(far_plane)) {
        error = "the bounds are too large to frame";
        return false;
    }
    far_plane = std::min(far_plane, kMaxFar);
    if (!(far_plane > near_plane)) {
        error = "the framed far plane " + std::to_string(far_plane) +
                " is not beyond the near plane " + std::to_string(near_plane);
        return false;
    }

    OrbitCamera framed;
    framed.yaw_deg = options.yaw_deg;
    framed.pitch_deg = options.pitch_deg;
    framed.distance = distance;
    framed.target = Vec3{centre[0], centre[1], centre[2]};
    framed.fov_y_deg = options.fov_y_deg;
    framed.near_plane = near_plane;
    framed.far_plane = far_plane;
    // Every clamp above is one of validate_camera's own bounds, so this is a
    // self-check rather than a second policy. It is run because a clamp that
    // stopped matching would otherwise produce a camera the renderer refuses
    // later, with an error naming the renderer rather than the framing.
    if (!validate_camera(framed, error)) {
        error = "the framed camera is invalid: " + error;
        return false;
    }
    camera = framed;
    return true;
}

bool render_track_draw_list(
    const PreparedTrackScene& scene,
    const TrackDrawList& list,
    const RenderOptions& options,
    const RenderLimits& limits,
    Frame& frame,
    RenderStats& stats,
    TrackRenderStats& track_stats,
    std::string& error,
    const ReferenceDrawObserver& observe) {
    stats = RenderStats{};
    track_stats = TrackRenderStats{};
    CameraFrame camera;
    if (!make_camera_frame(options.camera, std::max<std::uint32_t>(options.width, 1),
                           std::max<std::uint32_t>(options.height, 1), camera,
                           error)) {
        return false;
    }
    // Everything the loop below indexes is checked here, before the frame is
    // allocated: the scene's own invariants, then the list against the scene.
    for (const auto& texture : scene.region.textures) {
        if (texture.storage != RenderTextureStorage::rgba8) {
            error = "CPU rendering requires RGBA8 textures; GPU blocks are upload-only";
            return false;
        }
    }
    if (!validate_prepared_track_scene(scene, error)) {
        error = "invalid prepared track scene: " + error;
        return false;
    }
    if (!validate_track_draw_list(scene, list, error)) {
        error = "invalid track draw list: " + error;
        return false;
    }

    std::uint64_t source_triangles = 0;
    for (const PreparedTrackMesh& mesh : scene.region.meshes) {
        source_triangles += mesh.triangles.size();
    }
    if (source_triangles > limits.max_source_triangles ||
        source_triangles >= kNoTriangle) {
        error = "region has " + std::to_string(source_triangles) +
                " triangles; the limit is " +
                std::to_string(limits.max_source_triangles);
        return false;
    }
    // validate_track_draw_list recomputed cost.triangles from the entries, so
    // this is the number the loop below will actually submit.
    std::uint64_t submitted = list.cost.triangles;
    if (options.sky && scene.sky_mesh != kNoHandle) {
        for (const auto& draw : scene.region.draws) {
            if (draw.mesh != scene.sky_mesh || !draw.drawable ||
                draw.binding != BindingKind::resolved) continue;
            submitted += draw.index_count / 3;
        }
    }
    if (submitted > limits.max_submitted_triangles || submitted >= kNoTriangle) {
        error = "draw list submits " + std::to_string(submitted) +
                " triangles; the limit is " +
                std::to_string(limits.max_submitted_triangles);
        return false;
    }

    std::vector<unsigned char> id_fallback;
    std::vector<unsigned char> instance_drawn;
    std::vector<unsigned char> mesh_drawn;
    std::vector<unsigned char> group_named;
    try {
        id_fallback.reserve(static_cast<std::size_t>(submitted));
        instance_drawn.assign(scene.instances.size(), 0);
        mesh_drawn.assign(scene.region.meshes.size(), 0);
        group_named.assign(scene.region.draws.size(), 0);
    } catch (const std::bad_alloc&) {
        error = "not enough memory for the triangle table";
        return false;
    }

    // Measured before the frame is allocated, over exactly the vertices the
    // loop below will transform, so a caller can report the box the picture
    // covers next to the box the culler tested.
    if (!track_draw_list_bounds(scene, list, track_stats.bounds, error)) {
        return false;
    }

    if (!frame.reset(options.width, options.height, options.background, limits,
                     error)) {
        return false;
    }

    Vec3 light{kLightX, kLightY, kLightZ};
    // Keep the lighting vector on the same square-root path as the camera.
    {
        const double length =
            det_sqrt(light.x * light.x + light.y * light.y + light.z * light.z);
        if (length > 0.0) {
            light.x /= length;
            light.y /= length;
            light.z /= length;
        }
    }
    // Separate background pass; IDs stay kNoTriangle and depth stays clear.
    // Constant far depth is an approximation for the bounded race camera.
    const auto render_sky = [&]() {
      if (options.sky && scene.sky_mesh != kNoHandle) {
        RenderOptions sky_options = options;
        sky_options.cull = CullMode::none;
        RenderStats sky_stats;
        RenderLimits sky_limits = limits;
        sky_limits.max_pixel_tests -= stats.pixel_tests;
        sky_limits.max_raster_triangles -= stats.raster_triangles;
        Rasterizer sky_rasterizer(frame, sky_options, sky_limits, sky_stats);
        const auto& mesh = scene.region.meshes[scene.sky_mesh];
        for (std::size_t group = 0; group < scene.region.draws.size(); ++group) {
            const auto& draw = scene.region.draws[group];
            // The sky is a separate pass; unresolved groups cannot sample a
            // texture and are skipped without suppressing the world frame.
            if (draw.mesh != scene.sky_mesh || !draw.drawable ||
                draw.binding != BindingKind::resolved) continue;
            RasterMaterial material;
            material.texture = &scene.region.textures[draw.texture];
            material.depth_write = false; material.sky = true;
            material.alpha_test = render_alpha_test_enabled(material.texture->alpha_test_mode);
            material.blend = render_blend_mode_of(material.texture->alpha_blend_mode);
            // The sky copy is numbered one past the scenery instances, as
            // record_track_commands numbers it.
            if (observe) observe(ReferenceDraw{group, draw.mesh, scene.instances.size(),
                draw.texture, draw.first_index, draw.index_count, true, false, true,
                CullMode::none, material.blend, material.alpha_test, material.alpha_ref, true});
            for (std::size_t n = draw.first_index/3; n < (draw.first_index+draw.index_count)/3; ++n) {
                const auto& t = mesh.triangles[n];
                const std::uint16_t indices[] = {t.a,t.b,t.c};
                ClipVertex v[3];
                for (int k = 0; k < 3; ++k) {
                    const auto& source = mesh.vertices[indices[k]];
                    v[k] = project_point(camera, track_placed_vertex(source, scene.sky_transform));
                    v[k].z = v[k].w * 0.999999;
                    v[k].u = source.u; v[k].v = source.v;
                    v[k].r = source.color[0]; v[k].g = source.color[1]; v[k].b = source.color[2];
                    v[k].a = source.color[3];
                }
                if (!sky_rasterizer.draw_triangle(v,material,kNoTriangle,error)) return false;
            }
        }
        stats.pixel_tests += sky_stats.pixel_tests;
        stats.raster_triangles += sky_stats.raster_triangles;
      }
      return true;
    };
    bool sky_drawn = false;
    Rasterizer rasterizer(frame, options, limits, stats);
    std::uint32_t next_id = 0;
    stats.meshes = scene.region.meshes.size() +
                   scene.region.skipped_invalid_meshes.size();
    stats.meshes_skipped_invalid = scene.region.skipped_invalid_meshes.size();
    stats.meshes_rendered = scene.region.meshes.size();
    stats.unresolved_hashes = scene.region.unresolved_hashes.size();
    track_stats.entries = list.entries.size();

    // The bind key of the group last bound, in entry order. kNoHandle stands
    // for the one shared fallback binding every unresolved or unusable group
    // uses, which is the rule TrackDrawListCost::texture_binds applies.
    std::size_t bound = kNoHandle;
    bool has_bound = false;
    for (const TrackDrawListEntry& entry : list.entries) {
        const TrackDraw& draw = scene.region.draws[entry.draw];
        const PreparedTrackInstance& instance = scene.instances[entry.instance];
        const PreparedTrackMesh& mesh = scene.region.meshes[draw.mesh];
        if (group_named[entry.draw] == 0) {
            group_named[entry.draw] = 1;
            ++track_stats.groups;
        }
        RasterMaterial raster;
        std::size_t key = kNoHandle;
        switch (draw.binding) {
        case BindingKind::resolved:
            ++track_stats.entries_resolved;
            raster.texture = &scene.region.textures[draw.texture];
            key = draw.texture;
            if (options.material_state == MaterialStateMode::retail) {
                raster.alpha_test = render_alpha_test_enabled(
                    raster.texture->alpha_test_mode);
                raster.blend = render_blend_mode_of(
                    raster.texture->alpha_blend_mode);
                raster.depth_write = raster.blend == RenderBlendMode::disabled;
                stats.groups_alpha_tested += raster.alpha_test ? 1U : 0U;
                stats.groups_blended +=
                    raster.blend != RenderBlendMode::disabled ? 1U : 0U;
            }
            break;
        case BindingKind::unresolved:
            ++track_stats.entries_unresolved;
            break;
        default:
            ++track_stats.entries_unusable;
            break;
        }
        if (!has_bound || key != bound) {
            ++track_stats.texture_binds;
            bound = key;
            has_bound = true;
        }
        fallback_color(draw.texture_hash, raster.fallback);

        // Opaque depth rejects hidden sky pixels. Translucency must see
        // the sky as its destination colour, so stop before the first blend.
        // The parser excludes zero-index groups, matching the GPU path's
        // skip before it selects the first blended draw.
        if (!sky_drawn && raster.blend != RenderBlendMode::disabled) {
            if (!render_sky()) return false;
            sky_drawn = true;
        }

        const std::uint64_t first = draw.first_index;
        const std::uint64_t count = draw.index_count;
        if (observe && count != 0) {
            observe(ReferenceDraw{entry.draw, draw.mesh, entry.instance,
                raster.texture ? draw.texture : kNoHandle,
                draw.first_index, draw.index_count, true, raster.depth_write, true,
                options.cull, raster.blend, raster.alpha_test, raster.alpha_ref});
        }
        if ((first + count) / 3 > first / 3) {
            ++track_stats.entries_drawn;
            if (instance_drawn[entry.instance] == 0) {
                instance_drawn[entry.instance] = 1;
                ++track_stats.instances_drawn;
            }
            if (mesh_drawn[draw.mesh] == 0) {
                mesh_drawn[draw.mesh] = 1;
                ++track_stats.meshes_drawn;
            }
        }
        for (std::uint64_t t = first / 3; t < (first + count) / 3; ++t) {
            const std::uint32_t id = next_id++;
            const MeshTriangle& triangle =
                mesh.triangles[static_cast<std::size_t>(t)];
            ++stats.triangles_submitted;
            id_fallback.push_back(raster.texture == nullptr ? 1 : 0);
            ++(raster.texture == nullptr ? stats.triangles_fallback
                                         : stats.triangles_resolved);
            const std::uint16_t indices[3] = {triangle.a, triangle.b, triangle.c};
            if (indices[0] >= mesh.vertices.size() ||
                indices[1] >= mesh.vertices.size() ||
                indices[2] >= mesh.vertices.size()) {
                ++stats.triangles_bad_index;
                continue;
            }
            ClipVertex clip[3];
            Vec3 world[3];
            for (int k = 0; k < 3; ++k) {
                const TrackVertex& source = mesh.vertices[indices[k]];
                world[k] = track_placed_vertex(source, instance.transform);
                clip[k] = project_point(camera, world[k]);
                clip[k].u = source.u;
                clip[k].v = source.v;
                clip[k].r = source.color[0];
                clip[k].g = source.color[1];
                clip[k].b = source.color[2];
                clip[k].a = source.color[3];
            }
            // The same flat model the car path uses: a geometric normal,
            // flipped towards the eye, against the fixed light. A track
            // vertex carries no normal of its own (include/nfsu/track.h), so
            // there is no second source to prefer.
            const Vec3 e0{world[1].x - world[0].x, world[1].y - world[0].y,
                          world[1].z - world[0].z};
            const Vec3 e1{world[2].x - world[0].x, world[2].y - world[0].y,
                          world[2].z - world[0].z};
            Vec3 normal{e0.y * e1.z - e0.z * e1.y, e0.z * e1.x - e0.x * e1.z,
                        e0.x * e1.y - e0.y * e1.x};
            const double length = det_sqrt(normal.x * normal.x +
                                           normal.y * normal.y +
                                           normal.z * normal.z);
            const bool has_normal = length > 0.0 && finite_value(length);
            if (has_normal) {
                normal.x /= length;
                normal.y /= length;
                normal.z /= length;
            } else {
                ++stats.triangles_degenerate_source;
            }
            if (has_normal) {
                const double to_eye = normal.x * (camera.eye.x - world[0].x) +
                                      normal.y * (camera.eye.y - world[0].y) +
                                      normal.z * (camera.eye.z - world[0].z);
                if (to_eye < 0.0) {
                    normal = {-normal.x, -normal.y, -normal.z};
                }
            }
            raster.shade = 256;
            if (options.shading == ShadingMode::flat && has_normal) {
                const double d = normal.x * light.x + normal.y * light.y +
                                 normal.z * light.z;
                const double k = 0.35 + 0.65 * std::max(0.0, d);
                raster.shade = static_cast<std::uint32_t>(
                    std::floor(std::clamp(k, 0.0, 1.0) * 256.0 + 0.5));
            }
            if (!rasterizer.draw_triangle(clip, raster, id, error)) {
                return false;
            }
        }
    }

    if (!sky_drawn && !render_sky()) return false;

    // The copies that reached the frame. Every copy is placed by
    // track_instance_transform over the record the file stores, which is the
    // recovered placement, so none of them is a stand-in.
    stats.instances = track_stats.instances_drawn;
    stats.instances_exact = track_stats.instances_drawn;
    stats.instances_stand_in = 0;

    bool first_pixel = true;
    for (std::uint32_t y = 0; y < frame.height(); ++y) {
        for (std::uint32_t x = 0; x < frame.width(); ++x) {
            const std::uint32_t id = frame.ids()[std::size_t{y} * frame.width() + x];
            if (id == kNoTriangle) {
                continue;
            }
            ++stats.pixels_drawn;
            if (id < id_fallback.size() && id_fallback[id] != 0) {
                ++stats.pixels_fallback;
            }
            if (frame.blended()[std::size_t{y} * frame.width() + x] != 0) {
                ++stats.pixels_blended;
            }
            if (first_pixel) {
                stats.min_x = stats.max_x = x;
                stats.min_y = stats.max_y = y;
                first_pixel = false;
            } else {
                stats.min_x = std::min(stats.min_x, x);
                stats.max_x = std::max(stats.max_x, x);
                stats.min_y = std::min(stats.min_y, y);
                stats.max_y = std::max(stats.max_y, y);
            }
        }
    }
    return true;
}

} // namespace nfsu
