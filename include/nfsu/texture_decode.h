#pragma once

#include "nfsu/scene.h"
#include "nfsu/texture.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nfsu {

// Backend-neutral preparation of decoded textures for PreparedCarScene.
// This layer understands parsed texture packs; render backends do not.
struct RenderTextureLimits {
    // Upper bound on the RGBA8 bytes of all retained top levels together.
    std::uint64_t max_total_bytes = 128ULL * 1024ULL * 1024ULL;
    // Test hook called before each retained top-level allocation.
    std::function<void(std::uint64_t)> before_allocate;
};

// One parsed texture source. Every pack must have been parsed from exactly
// the bytes in this source. wanted, when present, is sorted ascending.
struct RenderTextureSource {
    const std::vector<unsigned char>* bytes = nullptr;
    std::vector<const TexturePack*> packs;
    bool complete = true;
    const std::vector<std::uint32_t>* wanted = nullptr;
};

// Decodes one parsed pack. Per-record failures remain in the result as
// unusable textures; fatal budget/allocation failures leave set empty.
bool decode_render_textures(
    const std::vector<unsigned char>& bytes,
    const TexturePack& pack,
    const RenderTextureLimits& limits,
    RenderTextureSet& set,
    std::string& error);

// Decodes sources in priority order. Earlier claims shadow later sources,
// including invalid claims. An incomplete source blocks every later source.
bool decode_render_texture_sources(
    const std::vector<RenderTextureSource>& sources,
    const RenderTextureLimits& limits,
    RenderTextureSet& set,
    std::string& error);

} // namespace nfsu
