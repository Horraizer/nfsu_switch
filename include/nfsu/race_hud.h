#pragma once

#include "nfsu/race_session.h"
#include "nfsu/race_lap_hud.h"
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
struct RaceHudReadout {
    std::uint64_t physics_step = 0;
    double engine_rpm = 0;
    double wheel_rpm = 0;
    double max_rpm = 0;
    double redline_rpm = 0;
    double rotation = 44;
    int gear = kVehicleGearNeutral;
    int mph = 0;
};
struct RaceHudFrame {
    std::array<TrackVertex, kRaceHudMaxQuads * 4> vertices{};
    std::uint16_t quads = 0;
    RaceHudReadout readout{};
    RaceLapHudPanel lap{};
    // 0x00427BB0..0x00427C41: the tach the HUD shows is this state, which
    // each update moves toward the engine speed by at most 50 rpm
    // (0x700DAC); the needle consumer 0x004A0FAF reads it, never the raw
    // engine field. The caller keeps one frame across updates; a fresh
    // frame starts at zero and sweeps up, as the original's zeroed state
    // does at startup.
    double needle_rpm = 0;
};

struct RaceHudDial {
    std::uint32_t lines_hash = 0;
    std::uint32_t red_hash = 0;
};

// Retail speed.exe selector at 0x00432A30 and needle map at 0x004A0EA0.
RaceHudDial select_race_hud_dial(double max_rpm, double redline_rpm) noexcept;
double race_hud_needle_rotation(double rpm, double max_rpm) noexcept;

bool load_race_hud_atlas(const std::filesystem::path& root,
                         RaceHudAtlas& atlas, std::string& error);
// Writes fixed-capacity vertices, reading only the completed race state.
// FEng local geometry under the measured anchor in docs/HUD_253.md.
void build_race_hud_frame(const RaceSession& session,
                          std::uint32_t width, std::uint32_t height,
                          RaceHudFrame& frame) noexcept;
// Pure geometry entry point for synthetic gear, speed and RPM checks.
void build_race_hud_frame_for_car(const VehicleState& car,
                                  double max_rpm, double redline_rpm,
                                  std::uint32_t width, std::uint32_t height,
                                  RaceHudFrame& frame) noexcept;
// CPU reference for exactly the quads sent to the final deko3d pass.
void composite_race_hud(const RaceHudAtlas& atlas, const RaceHudFrame& frame,
                         unsigned char* rgba, std::uint32_t width,
                         std::uint32_t height, std::uint32_t pitch) noexcept;

} // namespace nfsu
