#pragma once

#include "nfsu/race_session.h"
#include "nfsu/race_lap_hud.h"
#include "nfsu/track.h"
#include "nfsu/minimap.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// Retail texels are loaded from the user's installation at startup. This
// atlas, and all geometry below, are transient and never serialized.
constexpr std::uint32_t kRaceHudAtlasSize = 512;
constexpr std::uint32_t kRaceHudAtlasWidth = 1024;
constexpr std::size_t kRaceHudMaxQuads = 256;
struct RaceHudTextGlyph {
    int x = 0, y = 0, width = 0, height = 0;
    int bearing_x = 0, bearing_y = 0, advance = 0;
};
struct RaceHudAtlas {
    std::vector<unsigned char> rgba;
    // English FNTF metrics, with glyph texels packed into unused atlas space.
    // Small time font, large lap label/total font, LCD lap number.
    std::array<std::array<RaceHudTextGlyph,128>,3> lap_fonts{};
    bool lap_ready = false;
    bool boost_ready = false;
    MinimapResource map;
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
    std::uint16_t lap_first_quad = kRaceHudMaxQuads;
    RaceHudReadout readout{};
    RaceLapHudPanel lap{};

};

// Completed presentation intervals, oldest to newest; no allocation/readback.
struct RaceFpsHistory {
    std::array<double,90> intervals{};
    std::size_t count = 0;
    void add(double milliseconds) noexcept;
    double mean_ms() const noexcept;
};
void append_race_fps_overlay(const RaceHudAtlas& atlas, const RaceFpsHistory& history,
                             unsigned width, unsigned height, RaceHudFrame& frame) noexcept;

struct RaceMinimapState {
    bool initialized = false;
    std::int32_t world_ticks = 0;
    std::uint32_t resets = 0;
    MapPoint direction{}, velocity{};
};
void update_race_minimap_direction(MapPoint target, std::int32_t ticks,
                                   std::uint32_t resets, RaceMinimapState& state) noexcept;
// Single currently driven player, first track. Rendering only; no actor roster.
bool append_race_minimap(const RaceSession& session, const RaceHudAtlas& atlas,
                         unsigned width, unsigned height, RaceMinimapState& state,
                         RaceHudFrame& frame) noexcept;

struct RaceHudDial {
    std::uint32_t lines_hash = 0;
    std::uint32_t red_hash = 0;
};

// Retail speed.exe selector at 0x00432A30 and needle map at 0x004A0EA0.
RaceHudDial select_race_hud_dial(double max_rpm, double redline_rpm) noexcept;
double race_hud_needle_rotation(double rpm, double max_rpm) noexcept;

bool load_race_hud_atlas(const std::filesystem::path& root,
                         RaceHudAtlas& atlas, std::string& error, bool first_track_map = false,
                         bool boost = false);
void append_race_boost_hud(const RaceHudAtlas& atlas, const NitrousSnapshot& nitrous,
                          bool turbo_installed, float turbo_pressure, unsigned width,
                          unsigned height, RaceHudFrame& frame) noexcept;
// Writes fixed-capacity vertices, reading only the completed race state.
// FEng local geometry under the measured anchor in docs/HUD_253.md.
void build_race_hud_frame(const RaceSession& session,
                          std::uint32_t width, std::uint32_t height,
                          RaceHudFrame& frame) noexcept;
// English single-player circuit/free-lap presentation. Does not advance time.
void build_race_hud_frame(const RaceSession& session, const RaceHudAtlas& atlas,
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
