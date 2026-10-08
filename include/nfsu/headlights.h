#pragma once
#include "nfsu/render.h"

namespace nfsu {
bool load_headlight_texture(const std::filesystem::path& root,
                           RenderTexture& texture, std::string& error);
// Race camera setup from 0x40A140: body origin, forward and up in Y-up
// renderer coordinates. The caller retains ownership of the texture.
bool prepare_headlight_projection(const RenderTexture& texture, const Vec3& origin,
    const Vec3& forward, const Vec3& up, HeadlightProjection& result, std::string& error);
double headlight_clip_sample(double forward_distance) noexcept;
}
