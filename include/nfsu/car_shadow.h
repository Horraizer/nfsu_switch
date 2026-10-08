#pragma once

#include "nfsu/draw_commands.h"
#include "nfsu/race_session.h"
#include "nfsu/stock_frame.h"

#include <array>
#include <string>

namespace nfsu {

// The retail first ground-darkening layer. The mesh and texture are stable;
// only the placement and alpha change between frames.
struct CarShadowAsset {
    std::array<MeshVertex, 16> vertices{};
    std::array<MeshTriangle, 18> triangles{};
    RenderTexture texture;
    // The texture's own name, "<car>_SHADOWIG", for every message and
    // recorded upload name about it.
    std::string texture_name;
    bool selected_bounds = false;
    std::array<float,3> low{}, high{};
};

struct CarShadowPlacement {
    DrawInstanceUniform transform;
    float alpha = 0.0F;
    bool projected = false;
    std::array<float,16> road_heights{};
    unsigned strip_mask = 0;
};

// The shadow texture is the car's own, named "<car type name>_SHADOWIG" in
// its TEXTURES.BIN (issue #315: every retail car directory holds one). The
// name is looked up by its bin_hash in `pack`.
bool prepare_car_shadow(const std::string& car_type_name, const StockFrameInputs& inputs,
                        const TexturePack& pack, CarShadowAsset& out, std::string& error);
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
