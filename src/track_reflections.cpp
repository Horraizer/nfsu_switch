#include "nfsu/track_reflections.h"
#include <algorithm>
#include <array>
#include <limits>

namespace nfsu {
void prepare_track_reflection_cache(const PreparedTrackScene& scene, TrackReflectionCache& cache) {
    // A separate conservative reflection visibility query. It uses the actual
    // slot-4 bounds, transformed into the reflection view, not main-view hits.
    cache.bounds.resize(scene.region.meshes.size());
    auto& bounds = cache.bounds;
    cache.groups.clear(); cache.groups.resize(scene.region.meshes.size());
    auto& groups = cache.groups;
    for (std::size_t m = 0; m < bounds.size(); ++m) {
        auto& b = bounds[m];
        b = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
             std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        for (const auto& v : scene.region.meshes[m].vertices) {
            const float p[3] = {v.px, v.pz, v.py};
            for (int k = 0; k < 3; ++k) { b[k] = std::min(b[k], p[k]); b[k+3] = std::max(b[k+3], p[k]); }
        }
    }
    for (std::size_t d = 0; d < scene.region.draws.size(); ++d) {
        const auto& draw = scene.region.draws[d];
        if (draw.drawable && draw.index_count) groups.at(draw.mesh).push_back(static_cast<std::uint32_t>(d));
    }
}
TrackTransform track_reflection_transform(const PreparedTrackInstance& instance) {
    auto result = instance.transform;
    // 0x446063..0x4460A6 bypasses the matrix path for baked placements.
    if (!(instance.instance_flags & 0x200U)) result.linear[8] = -result.linear[8];
    return result;
}
bool track_reflection_entries(const PreparedTrackScene& scene, const RenderOptions& options,
                              std::vector<TrackDrawListEntry>& entries, std::string& error,
                              const TrackReflectionCache* cache, bool cull) {
    entries.clear();
    CameraFrame camera;
    if (!make_camera_frame(options.camera, options.width, options.height, camera, error)) return false;
    TrackReflectionCache local;
    if (!cache) { prepare_track_reflection_cache(scene, local); cache = &local; }
    const auto& bounds = cache->bounds;
    const auto& groups = cache->groups;
    std::uint64_t triangles = 0;
    for (std::size_t n = 0; n < scene.instances.size(); ++n) {
        const auto& instance = scene.instances[n];
        if (((instance.instance_flags ^ 0x60U) & 0x50U) != 0 || instance.reflection_mesh == kNoReflectionMesh) continue;
        if (instance.reflection_mesh >= bounds.size()) { error = "reflection mesh out of range"; return false; }
        if (groups[instance.reflection_mesh].empty()) continue;
        const auto& b = bounds[instance.reflection_mesh];
        const auto matrix = track_reflection_transform(instance);
        unsigned outside = 63;
        for (int corner = 0; corner < 8; ++corner) {
            float p[3] = {b[(corner & 1) ? 3 : 0], b[(corner & 2) ? 4 : 1], b[(corner & 4) ? 5 : 2]};
            double raw[3];
            for (int j = 0; j < 3; ++j) raw[j] = matrix.translation[j] + p[0]*double(matrix.linear[j]) +
                p[1]*double(matrix.linear[3+j]) + p[2]*double(matrix.linear[6+j]);
            const auto v = project_point(camera, {raw[0], raw[2], -raw[1]});
            const unsigned mask = (v.x < -v.w ? 1U : 0U) | (v.x > v.w ? 2U : 0U) |
                (v.y < -v.w ? 4U : 0U) | (v.y > v.w ? 8U : 0U) |
                (v.w < camera.near_plane ? 16U : 0U) | (v.w > camera.far_plane ? 32U : 0U);
            outside &= mask;
        }
        if (cull && outside) continue;
        for (auto d : groups[instance.reflection_mesh]) {
            triangles += scene.region.draws[d].index_count / 3;
            if (triangles > RenderLimits{}.max_submitted_triangles || (cull && entries.size() >= 4096)) {
                error = "reflection draw capacity exceeded"; return false;
            }
            entries.push_back({static_cast<std::uint32_t>(n), d});
        }
    }
    return true;
}
}
