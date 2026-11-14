#pragma once

#include "nfsu/race_session.h"
#include "nfsu/scene.h"
#include "nfsu/track_ground.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// Visual state only. It is never encoded into RaceSession or fed into physics.
// Smoke emission keeps a120Hz visual cadence while physics supplies its
// own interval. Particle lifetimes use elapsed time, not a fixed slice count.
enum class RaceParticleKind : std::uint8_t { smoke, spark };
enum class RaceParticleAtlas : unsigned { smoke, sparks, dust, colored, water_spray, electrical, birds, fire, count };

struct RaceParticleAtlasInfo {
    const char* name;
    std::uint32_t hash;
    unsigned width, height, columns, rows, first_cell;
};
inline constexpr std::array<RaceParticleAtlasInfo,8> kRaceParticleAtlases{{
    {"SMOKE_CS",       0x09DC04B3U, 128,128,4,4, 0},
    {"XE_SPARKSA",     0xA4C28190U, 256,128,4,2,16},
    {"SMOKEB_CS",      0x454D2975U, 256,256,4,4,24},
    {"SFX_SMOKE_A_CS", 0x0DE30D23U,  64, 64,2,2,40},
    {"DUSTA_CS",       0xA3C7BF15U, 256,128,4,2,44},
    {"SFX_RJBSPARKS",  0x75B5D261U,  64, 32,4,2,52},
    {"FLOCKOFSEAGULLS",0x9F7A8CA3U, 128,128,4,4,60},
    {"FIREA_CS",       0xDD2BF79BU, 256,128,4,2,76},
}};
constexpr unsigned kRaceParticleAtlasCells = 84;
constexpr std::size_t kRaceVehicleAtlasCount = 5;
constexpr unsigned kRaceWaterPresetCount = 19;

enum class RaceParticleEffect : unsigned {
    fixture, road, rough_road, dirt, grass, blue, yellow, red, wall,
    water_first, purge_first = water_first + kRaceWaterPresetCount,
    purge_right, car_impact, count
};

struct RaceParticlePreset {
    std::array<std::array<float,4>,4> rgba{};
    std::array<float,4> size{};
    float life=0, life_spread=0, speed=0, speed_spread=0;
    float arc=0, tilt=0, inherit=0, inherit_spread=0, gravity=0, drag=0;
    float interval=1;
    RaceParticleAtlas atlas = RaceParticleAtlas::smoke;
    unsigned frame_period=0, wheel=0;
};

struct RaceParticle {
    RaceParticleKind kind = RaceParticleKind::smoke;
    std::uint64_t born_step = 0;
    double solver_dt = kVehicleStepSeconds;
    double elapsed_seconds = -1; // Managed age after a world-dt transition.
    VehicleVec3 origin;
    VehicleVec3 velocity;
    double size = 0.0; // initial diameter in metres
    unsigned initial_cell = 0;
    RaceParticleEffect effect = RaceParticleEffect::fixture;
    unsigned life_ticks = 0, frame_period = 0, environment_preset = 0;
    RaceParticleAtlas atlas = RaceParticleAtlas::smoke;
    VehicleVec3 displacement;
};

constexpr std::size_t kRaceSmokeLimit = 16;
constexpr std::size_t kRaceSparkLimit = 8;
constexpr std::size_t kRaceParticleLimit = kRaceSmokeLimit + kRaceSparkLimit;
constexpr std::size_t kRaceEnvironmentAlphaLimit = 24;
constexpr std::size_t kRaceEnvironmentAdditiveLimit = 16;
constexpr std::size_t kRaceEnvironmentLimit = kRaceEnvironmentAlphaLimit + kRaceEnvironmentAdditiveLimit;

// The slots retain insertion order within each kind. Expired particles are
// removed before emitting; at the cap, a new one replaces the oldest.
struct RacePairFrame;

class RaceParticles {
public:
    bool load_presets(const std::vector<unsigned char>& global, std::string& error);
    bool load_environment(const std::vector<unsigned char>& track, std::string& error);
    bool load_environment(const std::filesystem::path& track, std::string& error);
    void reset() noexcept;
    void advance_environment(std::uint64_t frame, const VehicleVec3& eye) noexcept;
    std::size_t environment_emitter_count() const noexcept { return emitters_.size(); }
    std::size_t environment_draw_limit() const noexcept {
        return emitters_.empty() ? 0 : kRaceEnvironmentLimit;
    }
    std::uint64_t environment_bytes() const noexcept;
    std::size_t environment_alpha_count() const noexcept { return environment_alpha_count_; }
    std::size_t environment_additive_count() const noexcept { return environment_additive_count_; }
    const RaceParticle& environment_alpha(std::size_t i) const noexcept { return environment_alpha_[i]; }
    const RaceParticle& environment_additive(std::size_t i) const noexcept { return environment_additive_[i]; }
    bool visual(const RaceParticle& particle, VehicleVec3& position, double& diameter,
                double& opacity, std::array<unsigned,3>& rgb) const noexcept;
    void advance(const RaceSession& session) noexcept;
    void advance(const VehicleTrackTelemetry& step, const VehicleSpec& spec,
                 float camera_distance = 0, unsigned driving_surface = 0,
                 const TrackContactWorld* contact_world = nullptr) noexcept;
    void emit_car_impact_sparks(const RacePairFrame&,const VehicleVec3& eye) noexcept;
    std::uint64_t step() const noexcept { return step_; }
    std::size_t smoke_count() const noexcept {
        return purge_count_ + std::min(smoke_count_,kRaceSmokeLimit-purge_count_);
    }
    std::size_t spark_count() const noexcept { return spark_count_; }
    const RaceParticle& smoke(std::size_t i) const noexcept {
        return i < purge_count_ ? purge_[i] : smoke_[i-purge_count_];
    }
    const RaceParticle& spark(std::size_t i) const noexcept { return sparks_[i]; }
private:
    struct EnvironmentPreset {
        RaceParticlePreset particle;
        float radius = 0, delay = 0, delay_spread = 0, rate_spread = 0;
        bool additive = false;
    };
    struct EnvironmentEmitter {
        VehicleVec3 origin, axis;
        unsigned preset = 0;
        std::uint32_t seed = 0, random = 0;
        double remaining = 0;
    };
    const RaceParticlePreset& preset_for(const RaceParticle&) const noexcept;
    void advance_purge(const RaceSession&) noexcept;
    std::array<VehicleVec3,2> purge_offsets_{};
    std::array<RaceParticle,kRaceSmokeLimit> purge_{};
    std::size_t purge_count_ = 0;
    std::uint64_t purge_frame_ = 0;
    std::vector<EnvironmentPreset> environment_presets_;
    std::vector<EnvironmentEmitter> emitters_;
    std::array<RaceParticle,kRaceEnvironmentAlphaLimit> environment_alpha_{};
    std::array<RaceParticle,kRaceEnvironmentAdditiveLimit> environment_additive_{};
    std::size_t environment_alpha_count_ = 0, environment_additive_count_ = 0;
    std::uint64_t environment_frame_ = 0;
    std::array<RaceParticlePreset,static_cast<unsigned>(RaceParticleEffect::count)> presets_{};
    bool authored_ = false;
    std::array<RaceParticle, kRaceSmokeLimit> smoke_{};
    std::array<RaceParticle, kRaceSparkLimit> sparks_{};
    std::array<float,kVehicleWheelCount> smoke_timers_{};
    std::array<double,kRaceWaterPresetCount> water_phase_{};
    TrackGroundCache water_ground_;
    std::size_t smoke_count_ = 0;
    std::size_t spark_count_ = 0;
    std::uint64_t step_ = 0;
    double seconds_=0,last_solver_dt_=0;
    bool variable_time_=false;
};

// A particle's evaluated state at the most recently completed physics step.
// Asset-free reference fixture. Real races use RaceParticles::visual.
bool race_particle_visual(const RaceParticle& particle, std::uint64_t step,
                          VehicleVec3& position, double& diameter,
                          double& opacity) noexcept;

struct TexturePackSet;
using RaceParticleTextures = std::array<RenderTexture,kRaceParticleAtlases.size()>;

bool decode_race_particle_textures(const std::vector<unsigned char>& bytes,
                                   const TexturePackSet& packs, RaceParticleTextures& textures,
                                   std::string& error, bool environment = false);
unsigned race_particle_atlas_cell(const RaceParticle& particle, std::uint64_t step) noexcept;

// Procedural fixture texel for the asset-free CPU renderer tests.
std::array<std::uint8_t,4> race_particle_texel(RaceParticleKind kind,
                                                unsigned x, unsigned y) noexcept;

// Reference overlay. Real races supply the same atlases as the GPU renderer.
bool draw_race_particles(std::vector<unsigned char>& rgba,
                         const RenderOptions& options,
                         const RaceParticles& particles,
                         std::string& error,
                         const Frame* track = nullptr,
                         const Frame* car = nullptr,
                         const RaceParticleTextures* textures = nullptr);

} // namespace nfsu
