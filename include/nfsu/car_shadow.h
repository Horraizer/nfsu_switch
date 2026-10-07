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
};

struct CarShadowPlacement {
    DrawInstanceUniform transform;
    float alpha = 0.0F;
};

// The shadow texture is the car's own, named "<car type name>_SHADOWIG" in
// its TEXTURES.BIN (issue #315: every retail car directory holds one). The
// name is looked up by its bin_hash in `pack`.
bool prepare_car_shadow(const std::string& car_type_name, const StockFrameInputs& inputs,
                        const TexturePack& pack, CarShadowAsset& out, std::string& error);
CarShadowPlacement place_car_shadow(const RaceSession& session) noexcept;
CarShadowPlacement place_car_shadow(const VehicleState& state,
                                    const VehicleSpec& spec) noexcept;
bool record_car_shadow_commands(const CarShadowAsset& asset,
                                const CarShadowPlacement& placement,
                                const RenderOptions& options,
                                RecordedCommands& out, std::string& error);
bool render_car_shadow(const CarShadowAsset& asset,
                       const CarShadowPlacement& placement,
                       const RenderOptions& options, Frame& frame,
                       RenderStats& stats, std::string& error);

} // namespace nfsu
