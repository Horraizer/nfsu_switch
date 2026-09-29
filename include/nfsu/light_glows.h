#pragma once

#include "nfsu/stock_frame.h"

#include <array>

namespace nfsu {

// Four marker positions, in the source car's raw (forward, left, up) frame.
// Missing markers stay absent; no invented position is drawn.
struct CarGlowAnchor {
    Vec3 slot{};
    bool present = false;
};
using CarGlowAnchors = std::array<CarGlowAnchor, 4>;

struct CarGlowSprite {
    Vec3 world{};
    double radius = 0.0;
    double opacity = 0.0;
    bool head = false;
};
using CarGlowSprites = std::array<CarGlowSprite, 4>;

CarGlowAnchors collect_car_glow_anchors(const StockFrame& stock) noexcept;
CarGlowSprites place_car_glows(const CarGlowAnchors& anchors,
                               const PreparedCarScene& car,
                               const CameraFrame& camera,
                               bool braking) noexcept;

// Procedural, bounded approximation of the retail flare image. The two
// 16x16 textures are generated at load time and are never stored in Git.
RenderTexture make_car_glow_texture(bool head);

// Two triangles per visible sprite, additive over the track depth buffer.
// The car is composed over that buffer afterwards by compose_race_frame.
bool draw_car_glows(Frame& track, const RenderOptions& options,
                    const CarGlowSprites& sprites, RenderStats& stats,
                    std::string& error);

// The placed.vert.glsl row-vector matrix for a camera-facing unit quad.
void car_glow_transform(const CarGlowSprite& sprite,
                        const CameraFrame& camera, float (&rows)[16]) noexcept;

} // namespace nfsu
