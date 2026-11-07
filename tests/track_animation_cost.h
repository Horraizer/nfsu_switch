// Immutable base payload pins predate explicit animated-frame residency.
// Count those trailing owned textures independently, keeping the base pin fixed.
#pragma once
#include "nfsu/track_scene.h"
#include <stdexcept>
inline std::uint64_t explicit_animation_bytes(const nfsu::PreparedTrackRegion& region) {
    if(region.animation_texture_count>region.textures.size())
        throw std::runtime_error("animation texture suffix outside scene");
    std::uint64_t bytes=0;
    for(std::size_t i=region.textures.size()-region.animation_texture_count;i<region.textures.size();++i) {
        const auto& texture=region.textures[i];
        if(!texture.usable()||texture.payload().empty())
            throw std::runtime_error("resident animation frame has no payload");
        bytes+=texture.payload().size();
    }
    return bytes;
}
