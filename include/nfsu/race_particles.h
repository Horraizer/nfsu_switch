#pragma once

#include "nfsu/race_session.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// Visual state only. It is never encoded into RaceSession or fed into physics.
// Smoke emission keeps a120Hz visual cadence while physics supplies its
// own interval. Particle lifetimes use elapsed time, not a fixed slice count.
enum class RaceParticleKind : std::uint8_t { smoke, spark };

struct RaceParticle {
    RaceParticleKind kind = RaceParticleKind::smoke;
    std::uint64_t born_step = 0;
    double solver_dt = kVehicleStepSeconds;
    double elapsed_seconds = -1; // Managed age after a world-dt transition.
    VehicleVec3 origin;
    VehicleVec3 velocity;
    double size = 0.0; // initial diameter in metres
};

constexpr std::size_t kRaceSmokeLimit = 16;
constexpr std::size_t kRaceSparkLimit = 8;
constexpr std::size_t kRaceParticleLimit = kRaceSmokeLimit + kRaceSparkLimit;

// The slots retain insertion order within each kind. Expired particles are
// removed before emitting; at the cap, a new one replaces the oldest.
class RaceParticles {
public:
    void reset() noexcept;
    void advance(const RaceSession& session) noexcept;
    void advance(const VehicleTrackTelemetry& step, const VehicleSpec& spec) noexcept;
    std::uint64_t step() const noexcept { return step_; }
    std::size_t smoke_count() const noexcept { return smoke_count_; }
    std::size_t spark_count() const noexcept { return spark_count_; }
    const RaceParticle& smoke(std::size_t i) const noexcept { return smoke_[i]; }
    const RaceParticle& spark(std::size_t i) const noexcept { return sparks_[i]; }
private:
    std::array<RaceParticle, kRaceSmokeLimit> smoke_{};
    std::array<RaceParticle, kRaceSparkLimit> sparks_{};
    std::size_t smoke_count_ = 0;
    std::size_t spark_count_ = 0;
    std::uint64_t step_ = 0;
    double seconds_=0,last_solver_dt_=0;
    bool variable_time_=false;
};

// A particle's evaluated state at the most recently completed physics step.
// Lifetime, rise, expansion and fading are explicit approximations.
bool race_particle_visual(const RaceParticle& particle, std::uint64_t step,
                          VehicleVec3& position, double& diameter,
                          double& opacity) noexcept;

// Small procedural soft disc, used identically by the CPU and GPU paths.
// It is an approximation, not a copy or extraction of a retail texture.
std::array<std::uint8_t,4> race_particle_texel(RaceParticleKind kind,
                                                unsigned x, unsigned y) noexcept;

// Reference image overlay. This uses the same procedural texels, fade and
// source-over/additive blend selection as the deko3d particle draws.
bool draw_race_particles(std::vector<unsigned char>& rgba,
                         const RenderOptions& options,
                         const RaceParticles& particles,
                         std::string& error,
                         const Frame* track = nullptr,
                         const Frame* car = nullptr);

} // namespace nfsu
