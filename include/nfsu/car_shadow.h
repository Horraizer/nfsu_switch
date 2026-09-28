#pragma once

#include "nfsu/draw_commands.h"
#include "nfsu/race_session.h"
#include "nfsu/stock_frame.h"

#include <array>

namespace nfsu {

// The retail first ground-darkening layer. The mesh and texture are stable;
// only the placement and alpha change between frames.
struct CarShadowAsset {
    std::array<MeshVertex, 16> vertices{};
    std::array<MeshTriangle, 18> triangles{};
    RenderTexture texture;
};

struct CarShadowPlacement {
    DrawInstanceUniform transform;
    float alpha = 0.0F;
};

bool prepare_car_shadow(const StockFrameInputs& inputs, const TexturePack& pack,
                        CarShadowAsset& out, std::string& error);
CarShadowPlacement place_car_shadow(const RaceSession& session) noexcept;
bool record_car_shadow_commands(const CarShadowAsset& asset,
                                const CarShadowPlacement& placement,
                                const RenderOptions& options,
                                RecordedCommands& out, std::string& error);
bool render_car_shadow(const CarShadowAsset& asset,
                       const CarShadowPlacement& placement,
                       const RenderOptions& options, Frame& frame,
                       RenderStats& stats, std::string& error);

} // namespace nfsu
