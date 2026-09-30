#pragma once
#include "nfsu/track_draw_list.h"
#include "nfsu/render.h"
#include "nfsu/gpu_upload.h"
#include "nfsu/track_contact.h"

namespace nfsu {
struct WetRoadLamp {
    float position[3]{};
    float direction[3]{};
    float reflected_height = 0;
};
struct WetRoadLamps {
    std::vector<WetRoadLamp> lights;
    RenderTexture texture;
    std::array<TrackVertex, 4> vertices{};
    std::array<std::uint16_t, 6> indices{{0, 1, 2, 0, 2, 3}};
};
bool load_wet_road_lamps(const std::filesystem::path& root, const TrackContactWorld& world,
                         WetRoadLamps& lamps, std::string& error);
bool wet_road_lamp_placement(const WetRoadLamp& lamp, const CameraFrame& camera, TrackTransform& out);
bool append_wet_lamp_uploads(const WetRoadLamps& lamps, UploadTable& uploads, std::string& error);
bool render_wet_road_lamps(const WetRoadLamps& lamps, const RenderOptions& options,
                           const RenderLimits& limits, Frame& frame, RenderStats& stats, std::string& error);

struct TrackReflectionCache {
    std::vector<std::array<float, 6>> bounds;
    std::vector<std::vector<std::uint32_t>> groups;
};
void prepare_track_reflection_cache(const PreparedTrackScene& scene, TrackReflectionCache& cache);
TrackTransform track_reflection_transform(const PreparedTrackInstance& instance);
bool track_reflection_entries(const PreparedTrackScene& scene, const RenderOptions& options,
                              std::vector<TrackDrawListEntry>& entries, std::string& error,
                              const TrackReflectionCache* cache = nullptr, bool cull = true);
}
