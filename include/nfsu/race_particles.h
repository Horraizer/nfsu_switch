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
    purge_right, car_impact, road_spray, car_impact_smoke, count
};

inline bool is_driving_water_effect(RaceParticleEffect effect) noexcept {
    return effect>=RaceParticleEffect::water_first && effect<RaceParticleEffect::purge_first;
}

struct RaceParticlePreset {
    std::array<std::array<float,4>,4> rgba{};
    std::array<float,4> size{};
    float life=0, life_spread=0, speed=0, speed_spread=0;
    float arc=0, tilt=0, inherit=0, inherit_spread=0, gravity=0, drag=0;
    float interval=1;
    VehicleVec3 offset;
    RaceParticleAtlas atlas = RaceParticleAtlas::smoke;
    unsigned frame_period=0;
    bool random_start_cell=true;
};

struct RaceParticle {
    RaceParticleKind kind = RaceParticleKind::smoke;
    std::uint8_t water_owner = 0;
    std::uint32_t water_node = 0;
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
    unsigned environment_source = 0;
    VehicleVec3 displacement;
};

constexpr std::size_t kRaceSmokeLimit = 16;
constexpr std::size_t kRaceWaterPool = 1000; // 40272B, shared native particle pool ceiling.
struct WaterParticleView {
    std::array<std::uint16_t,kRaceWaterPool> indices{};
    std::size_t count = 0;
};
constexpr std::size_t kRaceSparkLimit = 8;
constexpr std::size_t kRaceParticleLimit = kRaceSmokeLimit + kRaceSparkLimit;
// The slots retain insertion order within each kind. Expired particles are
// removed before emitting; at the cap, a new one replaces the oldest.
struct RacePairFrame;

class RaceParticles {
public:
    bool load_presets(const std::vector<unsigned char>& global, std::string& error);
    bool load_environment(const std::vector<unsigned char>& track, std::string& error);
    bool load_environment(const std::filesystem::path& track, std::string& error);
    void reset() noexcept;
    void advance_environment(std::uint64_t frame,const VehicleVec3& eye,const VehicleVec3* other_eye=nullptr) noexcept;
    std::size_t environment_emitter_count() const noexcept { return emitters_.size(); }
    std::size_t environment_draw_limit() const noexcept {
        return environment_draw_limit_;
    }
    std::uint64_t environment_bytes() const noexcept;
    std::size_t environment_alpha_count() const noexcept { return environment_alpha_count_; }
    std::size_t environment_additive_count() const noexcept { return environment_additive_count_; }
    const RaceParticle& environment_alpha(std::size_t i) const noexcept { return environment_alpha_[environment_order_[0][i]]; }
    const RaceParticle& environment_additive(std::size_t i) const noexcept { return environment_additive_[environment_order_[1][i]]; }
    // Native 404240 walks active placements, then each placement's children.
    template<class Visitor>
    void visit_environment(Visitor&& visit) const {
        for(std::size_t i=0;i<active_environment_count_;++i) {
            const auto& source=emitters_[active_environment_sources_[i]];
            const bool additive=environment_presets_[source.preset].additive;
            const auto& pool=additive?environment_additive_:environment_alpha_;
            const auto& order=environment_order_[additive];
            for(auto j=source.child_offset-source.children;j<source.child_offset;++j)
                visit(pool[order[j]]);
        }
    }
    bool visual(const RaceParticle& particle, VehicleVec3& position, double& diameter,
                double& opacity, std::array<unsigned,3>& rgb) const noexcept;
    void advance(const RaceSession& session,const RaceSession* second_driver=nullptr,bool vehicle_only=false) noexcept;
    void advance(const VehicleTrackTelemetry& step, const VehicleSpec& spec,
                 float camera_distance = 0, unsigned driving_surface = 0,
                 const TrackContactWorld* contact_world = nullptr, const VehicleVec3* camera_eye = nullptr,
                 const VehicleVec3* second_eye = nullptr) noexcept;
    void emit_car_impact_sparks(const RacePairFrame&,const VehicleVec3& eye,const VehicleVec3* other_eye=nullptr) noexcept;
    std::uint64_t step() const noexcept { return step_; }
    std::size_t smoke_count() const noexcept {
        return purge_count_ + std::min(spray_count_+smoke_count_,kRaceSmokeLimit-purge_count_);
    }
    WaterParticleView visible_water(const CameraFrame& camera) const noexcept;
    std::size_t water_count() const noexcept { return water_count_; }
    const RaceParticle& water(std::size_t i) const noexcept { return water_[i]; }
    std::size_t spark_count() const noexcept { return spark_count_; }
    const RaceParticle& smoke(std::size_t i) const noexcept {
        if (i < purge_count_) return purge_[i];
        i -= purge_count_;
        return i < spray_count_ ? spray_[i] : smoke_[i-spray_count_];
    }
    const RaceParticle& spark(std::size_t i) const noexcept { return sparks_[i]; }
private:
    struct EnvironmentPreset {
        RaceParticlePreset particle;
        VehicleVec3 offset, extent;
        float radius = 0, on_time = 0, on_spread = 0, off_time = 0, off_spread = 0, rate_spread = 0;
        float rate = 0;
        bool additive = false, planar = false, inherit_during_update = false;
    };
    struct EnvironmentEmitter {
        VehicleVec3 origin;
        std::array<VehicleVec3,3> basis;
        unsigned preset = 0;
        std::uint32_t seed = 0, random = 0;
        double remaining = 0, phase_time = 0;
        unsigned children = 0, child_offset = 0;
        bool active = true, listed = false;
    };
    RaceParticle spawn_environment(EnvironmentEmitter&,const EnvironmentPreset&) const noexcept;
    void index_environment_children() noexcept;
    const RaceParticlePreset& preset_for(const RaceParticle&) const noexcept;
    void advance_water(double frame_dt) noexcept;
    void advance_vehicle(const VehicleTrackTelemetry&,const VehicleSpec&,float,unsigned,
        const TrackContactWorld*,const VehicleVec3*,const VehicleVec3*) noexcept;
    void advance_purge(const RaceSession&) noexcept;
    void emit_wheel_smoke(const VehicleTrackTelemetry&,const VehicleSpec&,float,
                          std::array<float,kVehicleWheelCount>&,std::uint64_t,bool,unsigned) noexcept;
    std::array<VehicleVec3,2> purge_offsets_{};
    std::array<RaceParticle,kRaceSmokeLimit> purge_{};
    std::size_t purge_count_ = 0;
    std::uint64_t purge_frame_ = 0;
    std::vector<EnvironmentPreset> environment_presets_;
    std::vector<EnvironmentEmitter> emitters_;
    std::vector<unsigned> active_environment_sources_;
    std::size_t active_environment_count_ = 0, environment_draw_limit_ = 0;
    std::array<std::vector<std::uint16_t>,2> environment_order_;
    std::vector<RaceParticle> environment_alpha_;
    std::vector<RaceParticle> environment_additive_;
    std::size_t environment_alpha_count_ = 0, environment_additive_count_ = 0;
    std::uint64_t environment_frame_ = 0;
    std::array<RaceParticlePreset,static_cast<unsigned>(RaceParticleEffect::count)> presets_{};
    bool authored_ = false;
    std::array<RaceParticle, kRaceSmokeLimit> smoke_{};
    std::array<RaceParticle, kRaceSparkLimit> sparks_{};
    std::array<float,kVehicleWheelCount> smoke_timers_{};
    struct WaterEmitter {
        std::array<VehicleVec3,3> basis;
        VehicleVec3 offset, extent;
        unsigned marker = 0;
    };
    std::array<WaterEmitter,kRaceWaterPresetCount> water_emitters_{};
    std::array<unsigned,kRaceWaterPresetCount> water_placement_order_{};
    std::array<std::uint32_t,kRaceWaterPresetCount> cached_water_node_{};
    std::uint64_t water_frame_ = 0;
    double water_sample_seconds_ = 0;
    struct SpraySource {
        TrackGroundCache ground;
        double next_road = 0;
        unsigned surface=0;
    };
    void emit_driving_spray(const VehicleTrackTelemetry&,const VehicleSpec&,float,unsigned,
        const TrackContactWorld*,SpraySource&,unsigned) noexcept;
    void emit_track_sparks(const VehicleTrackTelemetry&,float,const TrackContactWorld*,
        const VehicleVec3*,const VehicleVec3*,std::uint64_t,unsigned) noexcept;
    struct RivalSmokeState {
        SpraySource spray;
        std::array<float,kVehicleWheelCount> timers{};
        std::uint64_t step=0;
    };
    std::array<RivalSmokeState,3> rival_smoke_{};
    std::array<RaceParticle,kRaceSmokeLimit/2> spray_{};
    std::vector<RaceParticle> water_;
    std::size_t water_count_ = 0;
    SpraySource player_spray_;
    std::size_t spray_count_ = 0;
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
