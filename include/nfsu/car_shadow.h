#pragma once

#include "nfsu/draw_commands.h"
#include "nfsu/race_session.h"
#include "nfsu/stock_frame.h"
#include "nfsu/track_ground.h"

#include <array>
#include <string>
#include <vector>

namespace nfsu {

// The retail first ground-darkening layer. The mesh and texture are stable;
// only the placement and alpha change between frames.
// Shadows and cinematic prediction use the same original440A40 selector.
using CarShadowGroundCache=TrackGroundCache;
inline bool query_car_shadow_ground(const TrackContactWorld& world,const VehicleVec3& p,
                                    CarShadowGroundCache& cache,TrackContact& hit) {
    return query_track_ground(world,p,cache,hit);
}
struct CarShadowAsset {
    std::array<MeshVertex, 16> vertices{};
    std::array<MeshTriangle, 18> triangles{};
    RenderTexture texture;
    mutable CarShadowGroundCache ground_cache;
    // The texture's own name, "<car>_SHADOWIG", for every message and
    // recorded upload name about it.
    std::string texture_name;
    bool selected_bounds = false;
    std::array<float,3> low{}, high{};
};

struct CarShadowPlacement {
    DrawInstanceUniform transform;
    float alpha = 0.0F;
    float strength = 0.0F; // unquantized native height/alignment fold for neon
    bool projected = false;
    std::array<float,16> road_heights{};
    unsigned strip_mask = 0;
};

// Separate high-quality in-race layer. The topology and upload slots are
// stable; only the road-projected vertices change between frames.
class CarShadowSilhouette {
public:
    bool initialize(const VehicleHull&, std::string&);
    bool append(UploadTable&, const RenderTexture&, std::size_t ramp_resource,
                RecordedDraw&, std::string&) const;
    void update(const VehicleState&, const VehicleSpec&, const TrackContactWorld&,
                const VehicleVec3& eye, const VehicleVec3& light_position, bool visible, RecordedDraw&);
    std::size_t resident_bytes() const noexcept;
private:
    static constexpr std::size_t kHullLimit = 64;
    std::vector<std::array<float,3>> hull_;
    std::vector<MeshVertex> vertices_;
    std::vector<MeshTriangle> triangles_;
    CarShadowGroundCache ground_cache_;
};

bool prepare_car_shadow_ramp(const StockFrameInputs&, const StockFrame&,
                             RenderTexture&, std::string&);
VehicleVec3 car_shadow_light_ray(const VehicleVec3& origin, const VehicleVec3& light_position) noexcept;

// The texture is "<car type name>_SHADOWIG". Most traffic models keep
// this texture in the shared car pack.
bool prepare_car_shadow(const std::string& car_type_name, const StockFrameInputs& inputs,
                        const TexturePack& pack, CarShadowAsset& out, std::string& error,
                        StockFrameFile texture_file = StockFrameFile::car_textures);
CarShadowPlacement place_car_shadow(const RaceSession& session) noexcept;
CarShadowPlacement place_car_shadow(const VehicleState& state,
                                    const VehicleSpec& spec) noexcept;
// Original selected LOD-A solid bounds, slots 0..11, with constructor padding.
bool prepare_car_shadow_bounds(const StockFrameInputs&, const StockFrame&,
                               CarShadowAsset&, std::string&);
using CarShadowRoadQuery = bool (*)(const VehicleVec3&, TrackContact&, const void*);
CarShadowPlacement project_car_shadow(const CarShadowAsset&, const VehicleState&,
    const VehicleSpec&, CarShadowRoadQuery, const void*);
CarShadowPlacement place_car_shadow(const CarShadowAsset&, const RaceSession&);
CarShadowPlacement place_car_shadow(const CarShadowAsset&,const VehicleState&,
                                    const VehicleSpec&,const TrackContactWorld&);
void update_car_shadow_draw(const CarShadowPlacement&, RecordedDraw&);
bool record_car_shadow_commands(const CarShadowAsset& asset,
                                const CarShadowPlacement& placement,
                                const RenderOptions& options,
                                RecordedCommands& out, std::string& error);
bool render_car_shadow(const CarShadowAsset& asset,
                       const CarShadowPlacement& placement,
                       const RenderOptions& options, Frame& frame,
                       RenderStats& stats, std::string& error);

} // namespace nfsu
