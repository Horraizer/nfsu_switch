#pragma once

#include "nfsu/stock_frame.h"

#include <array>

namespace nfsu {

// Five marker positions, in the source car's raw (forward, left, up) frame.
// Missing markers stay absent; no invented position is drawn.
struct CarGlowAnchor {
    Vec3 slot{};
    bool present = false;
    Vec3 normal{};
};
using CarGlowAnchors = std::array<CarGlowAnchor, 5>;

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

// Original primary-view light sprites: two layers per headlamp and three tails.
struct OriginalCarGlow {
    float transform[16]{};
    unsigned char rgba[4]{};
    unsigned texture = 0;
};
using OriginalCarGlows = std::array<OriginalCarGlow,7>;
bool load_original_car_glow_textures(const std::filesystem::path& root,
    std::array<RenderTexture,3>& textures,std::string& error);
OriginalCarGlows place_original_car_glows(const CarGlowAnchors& anchors,
    const PreparedCarScene& car,const CameraFrame& camera,unsigned display_width,
    double service_brake) noexcept;

// Procedural, bounded approximation of the retail flare image. The two
// 16x16 textures are generated at load time and are never stored in Git.
RenderTexture make_car_glow_texture(bool head);

// Two triangles per visible sprite, additive over the composed track/car
// depth buffer, after the opaque car draw and without depth writes.
bool draw_car_glows(Frame& track, const RenderOptions& options,
                    const CarGlowSprites& sprites, RenderStats& stats,
                    std::string& error);

// The placed.vert.glsl row-vector matrix for a camera-facing unit quad.
void car_glow_transform(const CarGlowSprite& sprite,
                        const CameraFrame& camera, float (&rows)[16]) noexcept;

} // namespace nfsu
