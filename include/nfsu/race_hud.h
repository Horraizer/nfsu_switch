#pragma once

#include "nfsu/race_session.h"
#include "nfsu/track.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// Retail texels are loaded from the user's installation at startup. This
// atlas, and all geometry below, are transient and never serialized.
constexpr std::uint32_t kRaceHudAtlasSize = 512;
constexpr std::size_t kRaceHudMaxQuads = 32;
struct RaceHudAtlas {
    std::vector<unsigned char> rgba;
};
struct RaceHudFrame {
    std::array<TrackVertex, kRaceHudMaxQuads * 4> vertices{};
    std::uint16_t quads = 0;
};

bool load_race_hud_atlas(const std::filesystem::path& root,
                         RaceHudAtlas& atlas, std::string& error);
// Writes fixed-capacity vertices, reading only the completed race state.
// The layout is an approximation from the owner's 1280x800 screenshots.
void build_race_hud_frame(const RaceSession& session,
                          std::uint32_t width, std::uint32_t height,
                          RaceHudFrame& frame) noexcept;
// CPU reference for exactly the quads sent to the final deko3d pass.
void composite_race_hud(const RaceHudAtlas& atlas, const RaceHudFrame& frame,
                         unsigned char* rgba, std::uint32_t width,
                         std::uint32_t height, std::uint32_t pitch) noexcept;

} // namespace nfsu
