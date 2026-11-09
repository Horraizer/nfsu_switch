#pragma once

#include "nfsu/fps_history.h"
#include "nfsu/race_session.h"
#include "nfsu/race_lap_hud.h"
#include "nfsu/track.h"
#include "nfsu/minimap.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
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
// Loaded from HUD_Drift.fng. No retail animation keys are compiled in.
struct DriftHudKey { unsigned tick=0; std::array<float,4> value{}; };
struct DriftHudChannel {
    unsigned kind=0,target=0,duration=0; bool loop=false;
    std::array<float,4> base{}; std::vector<DriftHudKey> keys;
};
struct DriftHudAnimation {
    std::string object,state; unsigned duration=0; bool return_to_init=false;
    std::vector<DriftHudChannel> channels;
};
struct DriftHudAppearance {
    std::array<unsigned char,3> color{{255,255,255}};
    unsigned char alpha=255; float scale=1;
    bool positioned=false;std::array<float,3> position{};
};
//4A83C0 display lifetime, kept per viewport rather than mutating scoring
// during a draw. Same-tick sampling is stable across repeated render passes.
struct DriftScoreDisplay {
    std::uint64_t sequence=0;
    std::int32_t shown=0;
    float points=0;int multiplier=1;
    bool failed=false,visible=false;
    std::array<unsigned char,4> color{{255,255,0,255}};
    void sample(const DriftScorePublication&,std::int32_t now,unsigned zone) noexcept;
};
struct DriftHudClock {
    bool initialized=false,warning=false,recovering=false,wrong_way=false;
    DriftScoreDisplay score;
    bool comment_hidden=false;std::int32_t comment_hidden_at=0;
    int multiplier=1; float total=0;
    std::int32_t now=0,multiplier_at=-100000,total_at=-100000,slow_at=0,wrong_at=0;
};
// Authored drag_HUD_test FEng objects, resolved once at load time.
struct RaceDragHudObject {
    std::string text;
    std::string name;
    unsigned type=0,font=0,justification=0;
    std::array<float,6> transform{{1,0,0,1,0,0}};
    std::array<float,6> parent_transform{{1,0,0,1,0,0}};
    std::array<float,2> position{},pivot{};
    float depth=0,width=0,height=0;
    std::array<float,4> uv{{0,0,1,1}};
    std::array<int,4> texture{};
    std::array<unsigned char,4> color{};
};
struct RaceHudAtlas {
    std::vector<unsigned char> rgba;
    unsigned height=kRaceHudAtlasSize;
    std::vector<RaceDragHudObject> drag_objects;
    RaceDragHudObject countdown,drift_comment;
    bool countdown_ready=false;
    std::vector<DriftHudAnimation> drag_animations;
    std::array<float,2> drag_rpm_angles{};
    std::array<std::string,4> drag_shift_labels;
    std::array<std::string,2> drag_failure_labels;
    // English FNTF metrics, with glyph texels packed into unused atlas space.
    // Small time font, large lap label/total font, LCD lap number.
    std::array<std::array<RaceHudTextGlyph,128>,3> lap_fonts{};
    std::array<float,3> lap_font_heights{};
    bool lap_ready = false;
    bool boost_ready = false;
    bool drift_ready = false;
    std::vector<DriftHudAnimation> drift_animations;
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
    int kph = 0;
};
struct RaceHudFrame {
    std::array<TrackVertex, kRaceHudMaxQuads * 4> vertices{};
    std::uint16_t quads = 0;
    unsigned atlas_height=kRaceHudAtlasSize;
    std::uint16_t lap_first_quad = kRaceHudMaxQuads;
    RaceHudReadout readout{};
    RaceLapHudPanel lap{};
    DriftHudClock drift_clock;
    bool metric_speed=false;
    bool shown_drift_notice=false;
    bool drag_lamp_selected=false;
    int drag_lamp=-1;
    std::int32_t drag_lamp_at=0;
    int drag_gear=-1;float drag_quality=0;
    std::int32_t drag_shift_at=-100000,drag_heat_at=-100000;
    bool drag_hot=false;

};

bool load_drift_hud_animations(const std::vector<unsigned char>& ingame,
                               std::vector<DriftHudAnimation>& result,std::string& error);
bool load_drag_hud_animations(const std::vector<unsigned char>& ingame,
                               std::vector<DriftHudAnimation>& result,std::string& error);
DriftHudAppearance sample_hud_animation(const std::vector<DriftHudAnimation>&,const char* object,
                                        const char* state,unsigned ticks) noexcept;
DriftHudAppearance sample_drift_hud_animation(const RaceHudAtlas&,const char* object,
                                              const char* state,unsigned ticks) noexcept;

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
// Player and explicit port rivals, first track. Rendering only.
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
// Original4A13A0 chooses drag300..60 or ordinary30..219 before4A1460.
float race_hud_turbo_rotation(float pressure,bool drag) noexcept;

bool load_race_hud_atlas(const std::filesystem::path& root,
                         RaceHudAtlas& atlas, std::string& error, bool first_track_map = false,
                         bool boost = false, bool drift = false, unsigned map_track = 0,
                         bool knockout = false);
bool load_race_drag_hud(const std::filesystem::path&,const std::string& car,const VehicleHandling&,
                        RaceHudAtlas&,std::string&);
void append_race_drag_hud(const RaceSession&,const RaceHudAtlas&,unsigned,unsigned,RaceHudFrame&) noexcept;
void append_race_boost_hud(const RaceHudAtlas& atlas, const NitrousSnapshot& nitrous,
                          bool turbo_installed, float turbo_pressure, unsigned width,
                          unsigned height, RaceHudFrame& frame, unsigned nitrous_count = 6) noexcept;
// Writes fixed-capacity vertices, reading only the completed race state.
// FEng local geometry under the measured anchor in docs/HUD_253.md.
void build_race_hud_frame(const RaceSession& session,
                          std::uint32_t width, std::uint32_t height,
                          RaceHudFrame& frame) noexcept;
// English single-player circuit/free-lap presentation. Does not advance time.
void build_race_hud_frame(const RaceSession& session, const RaceHudAtlas& atlas,
                          std::uint32_t width, std::uint32_t height,
                          RaceHudFrame& frame, bool metric_speed=false) noexcept;
// Original HUD_SingleRace animate state, sampled at 1200 Hz after finish.
void append_race_over(const RaceHudAtlas& atlas,double seconds,unsigned width,unsigned height,RaceHudFrame& frame,std::string_view message="Race Over") noexcept;
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
