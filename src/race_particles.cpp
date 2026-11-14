#include "nfsu/race_particles.h"
#include "nfsu/race_events.h"
#include "nfsu/texture_decode.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace nfsu {
namespace {
constexpr std::uint64_t kSmokeLife = 60;
constexpr std::uint64_t kSparkLife = 18;

struct WaterGroup {
    const char* name;
    unsigned count;
};
constexpr std::array<WaterGroup,4> kWaterGroups{{
    {"XETT_PUDDLE_DRSL",2}, {"XETT_PUDDLE_DR",3},
    {"XETT_DEEPWATER_DRSL",8}, {"XETT_DEEPWATER_DR",6},
}};
constexpr unsigned kWaterFirst = static_cast<unsigned>(RaceParticleEffect::water_first);
static_assert(kWaterGroups[0].count+kWaterGroups[1].count+
    kWaterGroups[2].count+kWaterGroups[3].count == kRaceWaterPresetCount);
bool water_surface(unsigned surface) noexcept { return surface==13 || surface==14; }

// 428722..4288CE. An airborne wheel skips the timer update; a grounded
// wheel below the speed band or beyond the camera gate resets it.
float wheel_smoke_strength(const VehicleWheelTelemetry& wheel, float dt,
                           float camera_distance, float& timer) noexcept {
    if (!wheel.contact) return 0;
    const auto& slip = wheel.effect_slip_velocity;
    const float speed = std::sqrt(slip[0]*slip[0] + slip[1]*slip[1]);
    const auto surface = wheel.surface;
    const bool has_effect = surface >= 1 && surface <= 16;
    const float minimum = has_effect ? 5.0F : (surface < 17 ? 50.0F :
                                               surface <= 21 ? 20.0F : 2.0F);
    if (!(camera_distance < 150.0F && speed > minimum)) {
        timer = 0;
        return 0;
    }
    timer += dt;
    if (!has_effect || timer < 0.15F) return 0;
    const float maximum = surface <= 7 ? 40.0F : 20.0F;
    const float ramp = timer < 0.7F ? 0.25F : timer < 2.0F ? 0.5F :
                       timer < 4.0F ? 0.75F : 1.0F;
    return std::min(1.0F, ((speed - minimum) / (maximum - minimum)) * ramp);
}

VehicleVec3 rotate(const VehicleQuat& q, const VehicleVec3& v) noexcept {
    const double xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z;
    const double xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
    const double wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
    return {
        (1-2*(yy+zz))*v.x + 2*(xy-wz)*v.y + 2*(xz+wy)*v.z,
        2*(xy+wz)*v.x + (1-2*(xx+zz))*v.y + 2*(yz-wx)*v.z,
        2*(xz-wy)*v.x + 2*(yz+wx)*v.y + (1-2*(xx+yy))*v.z};
}

VehicleVec3 add(const VehicleVec3& a, const VehicleVec3& b) noexcept {
    return {a.x+b.x, a.y+b.y, a.z+b.z};
}

// Stateless integer mixing. The step and wheel uniquely determine each
// direction; changing render frequency cannot change a particle.
std::uint32_t seed(std::uint64_t step, std::uint32_t wheel, std::uint32_t lane) noexcept {
    std::uint32_t x = static_cast<std::uint32_t>(step) ^
        static_cast<std::uint32_t>(step >> 32) ^ (wheel+1)*0x9E3779B9U ^
        (lane+1)*0x85EBCA6BU;
    x ^= x >> 16; x *= 0x7FEB352DU; x ^= x >> 15;
    x *= 0x846CA68BU; x ^= x >> 16;
    return x;
}

double jitter(std::uint64_t step, std::uint32_t wheel, std::uint32_t lane) noexcept {
    return static_cast<double>(seed(step,wheel,lane) & 0xFFFFU)/32767.5-1.0;
}

constexpr unsigned kParticleTicksPerSecond = 1024;

double particle_age(const RaceParticle& p, std::uint64_t step) noexcept {
    return p.elapsed_seconds >= 0 ? p.elapsed_seconds :
        static_cast<double>(step - p.born_step) * p.solver_dt;
}

double particle_life(const RaceParticle& p) noexcept {
    return p.life_ticks ? double(p.life_ticks) / kParticleTicksPerSecond :
        (p.kind == RaceParticleKind::smoke ? kSmokeLife : kSparkLife) * kVehicleStepSeconds;
}

RaceParticleEffect smoke_effect(unsigned surface) noexcept {
    switch (surface) {
    case 4: return RaceParticleEffect::blue;
    case 5: return RaceParticleEffect::yellow;
    case 6: return RaceParticleEffect::red;
    case 8: case 9: return RaceParticleEffect::dirt;
    case 10: case 11: case 12: case 16: return RaceParticleEffect::rough_road;
    case 15: return RaceParticleEffect::grass;
    case 13: case 14: return RaceParticleEffect::fixture; // No skid preset in GLOBALB.
    default: return RaceParticleEffect::road;
    }
}

double next_particle_random(std::uint32_t& state) noexcept {
    state ^= state << 17;
    state ^= state >> 13;
    state ^= state << 5;
    return double(state & 0x7FFFFFFFU) / 2147483648.0;
}

void spawn_particle(RaceParticle& p, const RaceParticlePreset& preset,
                    RaceParticleEffect effect, const VehicleVec3& carrier,
                    VehicleVec3 axis, std::uint32_t random) noexcept {
    const auto& atlas = kRaceParticleAtlases[static_cast<unsigned>(preset.atlas)];
    p.effect = effect;
    p.atlas = preset.atlas;
    p.frame_period = preset.frame_period;
    p.initial_cell = unsigned(next_particle_random(random) * atlas.columns * atlas.rows);
    const auto sample = [&](float base, float spread) {
        return base * (1.0 - spread) + next_particle_random(random) * base * spread;
    };
    p.life_ticks = std::max(1U, static_cast<unsigned>(
        sample(preset.life,preset.life_spread) * kParticleTicksPerSecond));
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const double angle = (next_particle_random(random) - .5) * preset.arc * radians;
    const double launch = sample(preset.speed,preset.speed_spread);
    const double inherited = sample(preset.inherit,preset.inherit_spread);
    const double tilt = (next_particle_random(random) - .5) * preset.tilt * radians;
    const double length = std::sqrt(axis.x*axis.x + axis.y*axis.y + axis.z*axis.z);
    if (length > 1e-6 && std::isfinite(length))
        axis = {axis.x/length,axis.y/length,axis.z/length};
    else
        axis = {0,0,1};
    const VehicleVec3 perpendicular = std::abs(axis.z) > .95 ? VehicleVec3{1,0,0} :
        VehicleVec3{-axis.y,axis.x,0};
    const double tangent_length = std::sqrt(perpendicular.x*perpendicular.x +
        perpendicular.y*perpendicular.y);
    const VehicleVec3 u{perpendicular.x/tangent_length,perpendicular.y/tangent_length,0};
    const VehicleVec3 v{-axis.z*u.y,axis.z*u.x,axis.x*u.y-axis.y*u.x};
    const double along = launch * std::cos(tilt);
    const double around = launch * std::sin(tilt);
    const double u_scale = around * std::cos(angle), v_scale = around * std::sin(angle);
    p.velocity = {axis.x*along+u.x*u_scale+v.x*v_scale+carrier.x*inherited,
                  axis.y*along+u.y*u_scale+v.y*v_scale+carrier.y*inherited,
                  axis.z*along+v.z*v_scale+carrier.z*inherited};
}

template<std::size_t N,typename Presets>
void update_particles(std::array<RaceParticle,N>& slots, std::size_t& count,
                      const Presets& presets, std::uint64_t step, double dt) noexcept {
    std::size_t keep = 0;
    for (std::size_t i=0; i<count; ++i) {
        auto& p = slots[i];
        if (step < p.born_step || particle_age(p,step) >= particle_life(p)) continue;
        if (p.life_ticks) {
            const auto& preset = presets(p);
            const auto damp = [&](double value) {
                return value * std::max(0.0,1.0-preset.drag*std::abs(value)*dt);
            };
            p.velocity = {damp(p.velocity.x),damp(p.velocity.y),
                          damp(p.velocity.z)-preset.gravity*dt};
            p.displacement = add(p.displacement,{p.velocity.x*dt,p.velocity.y*dt,p.velocity.z*dt});
        }
        slots[keep++] = p;
    }
    count = keep;
}

template<std::size_t N>
void emit(std::array<RaceParticle,N>& slots, std::size_t& count,
          const RaceParticle& particle) noexcept {
    if (count == N) {
        for (std::size_t i=1; i<N; ++i) slots[i-1] = slots[i];
        --count;
    }
    slots[count++] = particle;
}
std::uint32_t word(const unsigned char* p) noexcept {
        return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
            (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
float number(const unsigned char* p) noexcept {
        float value;
        const auto bits = word(p);
        std::memcpy(&value,&bits,sizeof value);
        return value;
}

bool read_preset(RaceParticlePreset& preset, const unsigned char* record,
             const char* name, std::string& error) {
    const auto atlas = std::find_if(kRaceParticleAtlases.begin(),kRaceParticleAtlases.end(),
        [&](const auto& value) { return value.hash == word(record+0x0c); });
    if (atlas == kRaceParticleAtlases.end() || record[0x19] > 2 || record[0x1a] > 2 ||
        (1U << record[0x19]) != atlas->columns || (1U << record[0x1a]) != atlas->rows) {
        error = std::string("invalid atlas for particle group ") + name;
        return false;
    }
    preset.atlas = static_cast<RaceParticleAtlas>(atlas-kRaceParticleAtlases.begin());
    preset.frame_period = record[0x1b] ? kParticleTicksPerSecond/record[0x1b] : 0;
    for (unsigned row=0; row<4; ++row) {
        preset.size[row] = number(record+0x140+row*16);
        for (unsigned c=0; c<4; ++c)
            preset.rgba[row][c] = number(record+0x100+row*16+c*4);
    }
    preset.life = number(record+0x38); preset.life_spread = number(record+0x3c);
    preset.speed = number(record+0x40); preset.speed_spread = number(record+0x44);
    preset.arc = number(record+0x48); preset.tilt = number(record+0x4c);
    preset.inherit = number(record+0x50); preset.inherit_spread = number(record+0x54);
    preset.gravity = number(record+0x84); preset.drag = number(record+0x88);
    const float rate = number(record+0x30);
    preset.interval = rate > 0 ? 1.0F/rate : 0.1F;
    const auto bounded = [](float v,float low,float high) { return v >= low && v <= high; };
    bool valid = bounded(preset.life,.001F,30) && bounded(preset.life_spread,0,1) &&
        bounded(preset.speed,0,100) && bounded(preset.speed_spread,0,1) &&
        bounded(preset.arc,0,360) && bounded(preset.tilt,0,360) &&
        bounded(preset.inherit,0,1) && bounded(preset.inherit_spread,0,1) &&
        bounded(preset.gravity,-50,50) && bounded(preset.drag,0,10) && bounded(rate,0,240);
    for (unsigned row=0; row<4; ++row) {
        valid = valid && bounded(preset.size[row],-1000,1000);
        for (float v : preset.rgba[row]) valid = valid && bounded(v,-10000,10000);
    }
    if (!valid) error = std::string("invalid coefficients for particle group ") + name;
    return valid;
}
} // namespace

bool RaceParticles::load_presets(const std::vector<unsigned char>& global, std::string& error) {
    RaceChunkPayload chunk;
    std::vector<RaceTableIssue> issues;
    if (!find_race_chunk(global.data(),global.size(),0x35021,chunk,issues)) {
        error = "GLOBALB.BUN has no particle preset chunk";
        return false;
    }
    const auto header = (chunk.begin + 15) & ~std::uint64_t{15};
    constexpr unsigned stride = 0x220;
    if (header + 16 > chunk.end || word(global.data()+header+8) != 4 ||
        word(global.data()+header+12) > (chunk.end-header-16)/stride) {
        error = "invalid GLOBALB.BUN particle preset table";
        return false;
    }
    constexpr const char* groups[]{
        "XETT_ROAD_SK","XETT_RRCSSW_SK","XETT_DIRTRD_SK","XETT_GRASS_SK",
        "XEM_ROADSKBLUE","XEM_ROADSKYELW","XEM_ROADSKRED","XECS_SOLID_WALL"
    };
    static_assert(std::size(groups)+1 == kWaterFirst);
    auto loaded = presets_;
    const auto rows = word(global.data()+header+12);
    const auto row_at = [&](unsigned row) { return global.data()+header+16+row*stride; };
    for (unsigned effect=0; effect<std::size(groups); ++effect) {
        const auto* name = groups[effect];
        const auto hash = bin_hash(name);
        const unsigned char* record = nullptr;
        for (unsigned i=0; i<rows; ++i) {
            const auto* row = row_at(i);
            if (word(row+0x10) != hash ||
                (effect+1 == static_cast<unsigned>(RaceParticleEffect::wall) &&
                 word(row+0x08) != bin_hash("XECS_SolidWall_SprksA"))) continue;
            if (record) {
                error = std::string("ambiguous particle group ") + name;
                return false;
            }
            record = row;
        }
        if (!record) {
            error = std::string("missing particle group ") + name;
            return false;
        }
        if (!read_preset(loaded[effect+1],record,name,error)) return false;
    }
    const auto car_sparks=bin_hash("XECI_Car_SprksA");
    const auto car_group=bin_hash("XECI_CAR");
    bool have_car_sparks=false;
    for(unsigned i=0;i<rows;++i) {
        const auto* row=row_at(i);
        if(word(row+0x08)!=car_sparks||word(row+0x10)!=car_group)continue;
        if(!read_preset(loaded[static_cast<unsigned>(RaceParticleEffect::car_impact)],row,"XECI_Car_SprksA",error))
            return false;
        have_car_sparks=true;
        break;
    }
    if(!have_car_sparks) {error="missing car impact spark preset";return false;}
    unsigned effect = kWaterFirst;
    for (const auto& group : kWaterGroups) {
        const auto hash = bin_hash(group.name);
        const unsigned end = effect + group.count;
        for (unsigned i=0; i<rows; ++i) {
            const auto* row = row_at(i);
            if (word(row+0x10) != hash) continue;
            const auto wheel = word(row+0x58);
            if (effect == end || wheel == 0 || wheel > kVehicleWheelCount) {
                error = std::string("invalid water emitter list ") + group.name;
                return false;
            }
            auto& preset = loaded[effect++];
            if (!read_preset(preset,row,group.name,error)) return false;
            preset.wheel = wheel-1;
        }
        if (effect != end) {
            error = std::string("incomplete water emitter list ") + group.name;
            return false;
        }
    }
    constexpr auto purge_first = static_cast<unsigned>(RaceParticleEffect::purge_first);
    constexpr auto purge_group = "CARFX_NOS_BLOWOFF";
    const auto purge_hash=bin_hash(purge_group);
    std::array<VehicleVec3,2> offsets{};
    unsigned purge_count = 0;
    for (unsigned i=0; i<rows; ++i) {
        const auto* row = row_at(i);
        if (word(row+0x10) != purge_hash) continue;
        if (purge_count==offsets.size()) {
            error = "ambiguous nitrous purge emitters";
            return false;
        }
        if (!read_preset(loaded[purge_first+purge_count],row,purge_group,error)) return false;
        // Car FX assets store lateral, vertical and longitudinal coordinates.
        const VehicleVec3 offset{number(row+0x68),number(row+0x60),number(row+0x64)};
        if (!(std::abs(offset.x)<10 && std::abs(offset.y)<10 && std::abs(offset.z)<10)) {
            error = "invalid nitrous purge emitter position";
            return false;
        }
        offsets[purge_count++] = offset;
    }
    if (purge_count!=offsets.size()) {
        error = "missing nitrous purge emitters";
        return false;
    }
    purge_offsets_ = offsets;
    presets_ = loaded;
    authored_ = true;
    reset();
    error.clear();
    return true;
}

bool RaceParticles::load_environment(const std::filesystem::path& path, std::string& error) {
    std::vector<unsigned char> track;
    std::vector<RaceTableIssue> issues;
    if (!load_race_image(path,track,issues)) {
        error = "cannot read track particle data";
        return false;
    }
    return load_environment(track,error);
}

bool RaceParticles::load_environment(const std::vector<unsigned char>& track, std::string& error) {
    constexpr std::uint32_t preset_tag = 0x35021, emitter_tag = 0x35020;
    constexpr std::size_t preset_stride = 0x220, emitter_stride = 0xc0;
    const auto table = [&](std::uint32_t tag, unsigned version, std::size_t stride,
                           unsigned& count) -> const unsigned char* {
        RaceChunkPayload chunk;
        std::vector<RaceTableIssue> issues;
        if (!find_race_chunk(track.data(),track.size(),tag,chunk,issues)) return nullptr;
        const auto header = (chunk.begin+15)&~std::uint64_t{15};
        if (header+16>chunk.end || word(track.data()+header+8)!=version) return nullptr;
        count=word(track.data()+header+12);
        if (count>(chunk.end-header-16)/stride) return nullptr;
        return track.data()+header+16;
    };
    unsigned emitter_count=0, preset_count=0;
    const auto* placements=table(emitter_tag,5,emitter_stride,emitter_count);
    const auto* definitions=table(preset_tag,4,preset_stride,preset_count);
    if (!placements || !definitions) {
        error="invalid track particle tables";
        return false;
    }
    std::map<std::uint32_t,const unsigned char*> definition_by_key;
    for (unsigned i=0;i<preset_count;++i) {
        const auto* row=definitions+i*preset_stride;
        if (!definition_by_key.emplace(word(row+8),row).second) {
            error="duplicate track particle preset";
            return false;
        }
    }
    std::map<std::uint32_t,unsigned> preset_by_key;
    std::vector<EnvironmentPreset> presets;
    std::vector<EnvironmentEmitter> emitters;
    emitters.reserve(emitter_count);
    for (unsigned i=0;i<emitter_count;++i) {
        const auto* row=placements+i*emitter_stride;
        const auto key=word(row+12);
        const auto definition=definition_by_key.find(key);
        if (word(row+0x10)!=1 || definition==definition_by_key.end()) {
            error="unresolved or unsupported track particle placement";
            return false;
        }
        const auto inserted=preset_by_key.emplace(key,static_cast<unsigned>(presets.size()));
        if (inserted.second) {
            const auto* data=definition->second;
            EnvironmentPreset preset;
            if (!read_preset(preset.particle,data,"track emitter",error)) return false;
            preset.radius=number(data+0x80);
            preset.delay=number(data+0x20);
            preset.delay_spread=number(data+0x24);
            preset.rate_spread=number(data+0x34);
            preset.additive=preset.particle.atlas==RaceParticleAtlas::electrical ||
                            preset.particle.atlas==RaceParticleAtlas::fire;
            if (!(preset.radius>0 && preset.radius<=10000 && preset.delay>=0 && preset.delay<=60 &&
                  preset.delay_spread>=0 && preset.delay_spread<=1 &&
                  preset.rate_spread>=0 && preset.rate_spread<=1)) {
                error="invalid track particle scheduling data";
                return false;
            }
            presets.push_back(preset);
        }
        EnvironmentEmitter emitter;
        emitter.origin={number(row+0x60),number(row+0x64),number(row+0x68)};
        emitter.axis={number(row+0x50),number(row+0x54),number(row+0x58)};
        const auto finite=[](const VehicleVec3& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        if (!finite(emitter.origin) || !finite(emitter.axis)) {
            error="invalid track particle transform";
            return false;
        }
        emitter.preset=inserted.first->second;
        emitter.seed=seed(word(row+8),i,key)|1U;
        emitters.push_back(emitter);
    }
    environment_presets_=std::move(presets);
    emitters_=std::move(emitters);
    reset();
    error.clear();
    return true;
}

std::uint64_t RaceParticles::environment_bytes() const noexcept {
    return environment_presets_.capacity()*sizeof(EnvironmentPreset)+
           emitters_.capacity()*sizeof(EnvironmentEmitter);
}

const RaceParticlePreset& RaceParticles::preset_for(const RaceParticle& p) const noexcept {
    return p.environment_preset ? environment_presets_[p.environment_preset-1].particle :
                                 presets_[static_cast<unsigned>(p.effect)];
}

void RaceParticles::advance_environment(std::uint64_t frame, const VehicleVec3& eye) noexcept {
    if (emitters_.empty() || frame<=environment_frame_) return;
    const double dt=std::min(frame-environment_frame_,std::uint64_t{4})*kDriveFrameSeconds;
    environment_frame_=frame;
    const auto preset=[this](const RaceParticle& p) -> const RaceParticlePreset& { return preset_for(p); };
    const auto update=[&](auto& pool, std::size_t& count) {
        for (std::size_t i=0;i<count;++i) pool[i].elapsed_seconds+=dt;
        update_particles(pool,count,preset,step_,dt);
    };
    update(environment_alpha_,environment_alpha_count_);
    update(environment_additive_,environment_additive_count_);

    // Keep the nearby emitters in each blend class. The original radius
    // gates admission; this extra cap bounds work and translucent draws.
    struct Candidate { double distance; unsigned emitter; };
    constexpr std::size_t active_per_blend=8;
    std::array<std::array<Candidate,active_per_blend>,2> nearest{};
    std::array<std::size_t,2> counts{};
    for (unsigned i=0;i<emitters_.size();++i) {
        const auto& emitter=emitters_[i];
        const auto& params=environment_presets_[emitter.preset];
        const double x=emitter.origin.x-eye.x,y=emitter.origin.y-eye.y,z=emitter.origin.z-eye.z;
        const double distance=x*x+y*y+z*z;
        if (!(distance<double(params.radius)*params.radius)) continue;
        auto& group=nearest[params.additive];
        auto& count=counts[params.additive];
        std::size_t position=count;
        if (position==group.size()) {
            if (distance>=group.back().distance) continue;
            --position;
        } else ++count;
        while (position && distance<group[position-1].distance) {
            group[position]=group[position-1];
            --position;
        }
        group[position]={distance,i};
    }
    for (unsigned group=0;group<nearest.size();++group) {
        for (std::size_t i=counts[group];i>0;--i) {
            auto& emitter=emitters_[nearest[group][i-1].emitter];
            const auto& params=environment_presets_[emitter.preset];
            const auto& definition=params.particle;
            emitter.remaining-=dt;
            if (emitter.remaining>0) continue;
            RaceParticle particle;
            particle.kind=params.additive?RaceParticleKind::spark:RaceParticleKind::smoke;
            particle.born_step=step_;
            particle.elapsed_seconds=0;
            particle.origin=emitter.origin;
            next_particle_random(emitter.random);
            spawn_particle(particle,definition,RaceParticleEffect::fixture,{},emitter.axis,emitter.random);
            particle.environment_preset=emitter.preset+1;
            if (params.additive) emit(environment_additive_,environment_additive_count_,particle);
            else emit(environment_alpha_,environment_alpha_count_,particle);
            const double rate_factor=1.0-params.rate_spread*next_particle_random(emitter.random);
            emitter.remaining=std::max(0.0,emitter.remaining)+definition.interval/rate_factor;
        }
    }
}

void RaceParticles::reset() noexcept {
    purge_count_ = 0;
    purge_frame_ = 0;
    smoke_count_ = spark_count_ = 0;
    step_ = 0;seconds_=last_solver_dt_=0;variable_time_=false;
    smoke_timers_ = {};
    water_phase_ = {};
    water_ground_ = {};
    environment_alpha_count_=environment_additive_count_=0;
    environment_frame_=0;
    for (auto& emitter : emitters_) {
        const auto& preset=environment_presets_[emitter.preset];
        emitter.random=emitter.seed;
        emitter.remaining=preset.delay*(1.0-preset.delay_spread*next_particle_random(emitter.random));
    }
}

void RaceParticles::advance_purge(const RaceSession& session) noexcept {
    // Countdown frames advance the world clock while physics steps remain zero.
    const auto frame = session.timing().step;
    if (!authored_ || frame<=purge_frame_) return;
    const double dt = session.world_frame_seconds();
    purge_frame_ = frame;
    for (std::size_t i=0; i<purge_count_; ++i) purge_[i].elapsed_seconds += dt;
    if (purge_count_) {
        const auto preset = [this](const RaceParticle& particle) -> const RaceParticlePreset& {
            return preset_for(particle);
        };
        update_particles(purge_,purge_count_,preset,step_,dt);
    }
    if (!session.nitrous_purging() || purge_count_==purge_.size()) return;

    constexpr auto first = static_cast<unsigned>(RaceParticleEffect::purge_first);
    const auto& car = session.car();
    const auto& com = session.spec().handling.centre_of_mass;
    const auto body_origin = add(car.position,rotate(car.orientation,{-com[0],-com[1],-com[2]}));
    const auto up = rotate(car.orientation,{0,0,1});
    // 428FAF supplies strength 1 to 569A00. 402E6D and 40396A consume
    // that as a burst budget, including the first emission at interval 0.
    // Keep the oldest live puffs so they can rise clear of the hood.
    const auto rounds=1U+unsigned(1.F/presets_[first].interval);
    // The two descriptors select marker bucket 1. 56BA1B..56BA66 fills
    // that bucket with the midpoint of the front wheel attachment points.
    const auto& wheels = session.spec().handling.wheels;
    const VehicleVec3 anchor{
        .5*(wheels[0].longitudinal+wheels[1].longitudinal),
        .5*(wheels[0].lateral+wheels[1].lateral),
        .5*(wheels[0].vertical+wheels[1].vertical)};
    std::array<VehicleVec3,2> origins;
    for (unsigned i=0; i<origins.size(); ++i)
        origins[i]=add(body_origin,rotate(car.orientation,add(anchor,purge_offsets_[i])));
    for (unsigned j=0; j<rounds&&purge_count_<purge_.size(); ++j) {
        for (unsigned i=0; i<origins.size()&&purge_count_<purge_.size(); ++i) {
            RaceParticle particle;
            particle.kind = RaceParticleKind::smoke;
            particle.born_step = step_;
            particle.elapsed_seconds = 0;
            particle.origin = origins[i];
            spawn_particle(particle,presets_[first+i],static_cast<RaceParticleEffect>(first+i),
                           car.velocity,up,seed(frame,i,first+j));
            purge_[purge_count_++]=particle;
        }
    }
}

void RaceParticles::advance(const RaceSession& session) noexcept {
    advance_purge(session);
    const auto& spec = session.spec();
    const auto& eye = session.camera().eye;
    const auto prior_step=step_;
    for (const auto& t : session.physics_telemetry()) {
        if (t.step <= step_) continue;
        const double x = t.position.x - eye.x;
        const double y = t.position.y - eye.y;
        const double z = t.position.z - eye.z;
        const auto distance = static_cast<float>(std::sqrt(x*x + y*y + z*z));
        unsigned surface = 0;
        if (authored_ && distance < 150 && std::any_of(t.model.wheels.begin(),t.model.wheels.end(),
            [](const auto& w) { return w.contact && water_surface(w.surface); })) {
            const auto& world = session.course().world;
            TrackContact ground;
            if (query_track_ground(world,t.position,water_ground_,ground))
                surface = world.collision.polygons[ground.polygon].unknown0[2];
        }
        advance(t,spec,distance,surface,&session.course().world);
    }
    if(step_!=prior_step)emit_car_impact_sparks(session.pair_contacts(),eye);
    advance_environment(session.world_visual_frame(),eye);
}

void RaceParticles::emit_car_impact_sparks(const RacePairFrame& frame,const VehicleVec3& eye) noexcept {
    if(!authored_||!step_||last_solver_dt_<=0)return;
    constexpr auto effect=RaceParticleEffect::car_impact;
    const auto& preset=presets_[static_cast<unsigned>(effect)];
    for(std::size_t i=0;i<frame.count;++i) {
        const auto& pair=frame.pairs[i];
        const auto& hit=pair.contact;
        if(!hit.applied||pair.other_kind!=0)continue;
        const auto& point=hit.point;
        if(!(std::hypot(point.x-eye.x,point.y-eye.y,point.z-eye.z)<50))continue;
        // 465A0E, CAR material +80/+84: the impact band starts above 10.
        // 465A8A selects impact group 5 and supplies a zero carrier velocity.
        const auto& j=hit.impulse;
        const auto magnitude=std::hypot(j[0],j[1],j[2]);
        if(!(magnitude>10))continue;
        const float strength=std::min((magnitude-10.F)/5.F,1.F);
        // Approximate the original emitter rate with the existing bounded burst.
        const unsigned count=strength>=.5?2:1;
        for(unsigned lane=0;lane<count;++lane) {
            RaceParticle spark;
            spark.kind=RaceParticleKind::spark;
            spark.origin=point;
            spark.born_step=step_;
            spark.solver_dt=last_solver_dt_;
            spark.elapsed_seconds=variable_time_?0:-1;
            spawn_particle(spark,preset,effect,{}, {},seed(step_,unsigned(i),lane));
            emit(sparks_,spark_count_,spark);
        }
    }
}

void RaceParticles::advance(const VehicleTrackTelemetry& t,
                            const VehicleSpec& spec, float camera_distance, unsigned driving_surface,
                            const TrackContactWorld* contact_world) noexcept {
        if (!t.step || t.step <= step_) return;
        if (step_ && t.step != step_+1) {
            smoke_count_ = spark_count_ = 0;
            smoke_timers_ = {};
            water_phase_ = {};
        }
        const auto previous_step=step_;
        step_ = t.step;
        const double solver_dt=t.model.solver_dt;
        if(!(solver_dt>0.0)||!std::isfinite(solver_dt))return;
        const double previous_seconds=seconds_;
        variable_time_=variable_time_||(last_solver_dt_>0&&last_solver_dt_!=solver_dt);
        seconds_=variable_time_?seconds_+solver_dt:double(step_)*solver_dt;
        last_solver_dt_=solver_dt;
        if(variable_time_) {
            const auto age=[&](auto& particles,std::size_t count){for(std::size_t i=0;i<count;++i){auto& p=particles[i];
                if(p.elapsed_seconds<0)p.elapsed_seconds=double(previous_step-p.born_step)*p.solver_dt;
                p.elapsed_seconds+=solver_dt;}};
            age(smoke_,smoke_count_);age(sparks_,spark_count_);
        }
        const auto preset=[this](const RaceParticle& p) -> const RaceParticlePreset& { return preset_for(p); };
        update_particles(smoke_,smoke_count_,preset,step_,solver_dt);
        update_particles(sparks_,spark_count_,preset,step_,solver_dt);
        if (t.reset != VehicleTrackReset::none) {
            smoke_count_ = spark_count_ = 0;
            smoke_timers_ = {};
            water_phase_ = {};
            return;
        }
        const auto& q = t.orientation;
        const auto& com = spec.handling.centre_of_mass;
        const VehicleVec3 body_origin = add(t.position,rotate(q,
            {-com[0],-com[1],-com[2]}));
        const double speed = std::sqrt(t.velocity.x*t.velocity.x+
                                       t.velocity.y*t.velocity.y);
        const auto emission_step=static_cast<std::uint64_t>(
            (variable_time_?seconds_:static_cast<double>(step_)*solver_dt)/kVehicleStepSeconds+1e-9);
        const auto previous_emission_step=static_cast<std::uint64_t>(
            (variable_time_?previous_seconds:static_cast<double>(step_-1)*solver_dt)/kVehicleStepSeconds+1e-9);
        for (std::uint32_t wheel=0; wheel<kVehicleWheelCount; ++wheel) {
            const auto& w = t.model.wheels[wheel];
            const float strength = wheel_smoke_strength(w, static_cast<float>(solver_dt),
                camera_distance, smoke_timers_[wheel]);
            if (strength <= 0 || emission_step==previous_emission_step ||
                emission_step % 4 != wheel) continue;
            const auto& geometry = spec.handling.wheels[wheel];
            const VehicleVec3 local{geometry.longitudinal,geometry.lateral,
                                    geometry.vertical-geometry.radius};
            RaceParticle p;
            p.kind = RaceParticleKind::smoke;
            p.born_step = step_;
            p.solver_dt = solver_dt;p.elapsed_seconds=variable_time_?0:-1;
            p.origin = add(body_origin,rotate(q,local));
            p.velocity = {t.velocity.x*0.20+jitter(emission_step,wheel,0)*0.28,
                          t.velocity.y*0.20+jitter(emission_step,wheel,1)*0.28,
                          0.35+jitter(emission_step,wheel,2)*0.08};
            p.size = 0.42+jitter(emission_step,wheel,3)*0.03;
            const auto effect = authored_ ? smoke_effect(w.surface) : RaceParticleEffect::fixture;
            if (authored_ && effect == RaceParticleEffect::fixture) continue;
            if (authored_) {
                const auto& preset = presets_[static_cast<unsigned>(effect)];
                const auto axis = rotate(q,{0,0,1});
                // Keep the staggered port schedule and two-particle bound.
                const unsigned burst = strength >= preset.interval ? 2U : 1U;
                for (unsigned lane=0; lane<burst; ++lane) {
                    spawn_particle(p,preset,effect,t.velocity,axis,seed(emission_step,wheel,7+lane));
                    emit(smoke_,smoke_count_,p);
                }
            } else {
                p.initial_cell = seed(emission_step,wheel,7) % 16;
                emit(smoke_,smoke_count_,p);
                if (strength >= 1.0F) {
                    p.initial_cell = seed(emission_step,wheel,8) % 16;
                    emit(smoke_,smoke_count_,p);
                }
            }
        }
        if (authored_ && camera_distance < 150 && water_surface(driving_surface) &&
            std::count_if(t.model.wheels.begin(),t.model.wheels.end(),
                [](const auto& w) { return w.contact; }) > 2) {
            // 428A31..428BD8 selects two driving groups from the ground
            // material beneath the body and the published speed in MPH.
            const double mph = std::hypot(speed,t.velocity.z)*2.237;
            const double drive = std::clamp((mph-35.0)/65.0,0.0,1.0);
            const double slide = std::clamp((mph-3.0)/27.0,0.0,1.0);
            const double strengths[]{std::max(0.0,slide-2*std::max(0.0,slide+drive-1)),drive};
            const bool deep = driving_surface == 14;
            const auto axis = rotate(q,{0,0,1});
            unsigned effect = kWaterFirst+(deep?kWaterGroups[0].count+kWaterGroups[1].count:0);
            for (unsigned group=0; group<2; ++group) {
                const auto end = effect+kWaterGroups[(deep?2:0)+group].count;
                for (; effect<end; ++effect) {
                    const auto& preset = presets_[effect];
                    auto& phase = water_phase_[effect-kWaterFirst];
                    if (strengths[group] <= 0) continue;
                    phase += solver_dt*strengths[group];
                    if (phase < preset.interval) continue;
                    phase = std::fmod(phase,preset.interval);
                    const auto& wheel = spec.handling.wheels[preset.wheel];
                    RaceParticle particle;
                    particle.born_step=step_;
                    particle.solver_dt=solver_dt;
                    particle.elapsed_seconds=variable_time_?0:-1;
                    particle.origin=add(body_origin,rotate(q,{wheel.longitudinal,wheel.lateral,
                        wheel.vertical-wheel.radius}));
                    spawn_particle(particle,preset,static_cast<RaceParticleEffect>(effect),t.velocity,
                                   axis,seed(emission_step,preset.wheel,effect));
                    emit(smoke_,smoke_count_,particle);
                }
            }
        }
        if (t.audio_contact_count && speed > 1.0) {
            const VehicleContactObservation* hit=nullptr;
            double strongest=0;
            for (std::size_t i=0; i<std::min(t.audio_contact_count,t.audio_contacts.size()); ++i) {
                const auto& c=t.audio_contacts[i];
                if (!std::isfinite(c.point.x)||!std::isfinite(c.point.y)||!std::isfinite(c.point.z)) continue;
                const double impulse=std::hypot(c.impulse.x,c.impulse.y,c.impulse.z);
                const bool solid_wall=authored_ && contact_world &&
                    c.polygon < contact_world->collision.polygons.size() &&
                    contact_world->collision.polygons[c.polygon].unknown0[2] == 20;
                // 465830 has separate scrape and impact bands for SOLID_WALL.
                // A fast tangential scrape can admit sparks on a light hit.
                const double strength=solid_wall ? (camera_distance < 50 ?
                    std::max(std::clamp((c.slide_mps-15.0)/35.0,0.0,1.0),
                             std::clamp((impulse-10.0)/5.0,0.0,1.0)) : 0) :
                    (t.impulses ? impulse : 0);
                if (std::isfinite(strength)&&strength>strongest) {
                    hit=&c;strongest=strength;
                }
            }
            if(!hit)return;
            const VehicleVec3 point=hit->point;
            for (std::uint32_t lane=0; lane<2; ++lane) {
                RaceParticle p;
                p.kind = RaceParticleKind::spark;
                p.born_step = step_;
                p.solver_dt = solver_dt;p.elapsed_seconds=variable_time_?0:-1;
                p.origin = point;
                p.velocity = {t.velocity.x*0.10+jitter(step_,lane,4)*2.0,
                              t.velocity.y*0.10+jitter(step_,lane,5)*2.0,
                              0.8+jitter(step_,lane,6)*0.6};
                p.size = 0.055;
                p.atlas = RaceParticleAtlas::sparks;
                p.initial_cell = seed(emission_step,lane,7) % 8;
                if (authored_) {
                    constexpr auto effect = RaceParticleEffect::wall;
                    spawn_particle(p,presets_[static_cast<unsigned>(effect)],effect,t.velocity,
                                   hit->normal,seed(emission_step,lane,7));
                }
                emit(sparks_,spark_count_,p);
            }
        }
}

bool race_particle_visual(const RaceParticle& particle, std::uint64_t step,
                          VehicleVec3& position, double& diameter,
                          double& opacity) noexcept {
    const auto life = particle.kind == RaceParticleKind::smoke ? kSmokeLife : kSparkLife;
    if (step < particle.born_step) return false;
    const double elapsed_steps=static_cast<double>(step-particle.born_step);
    const double life_steps=static_cast<double>(life)*kVehicleStepSeconds/particle.solver_dt;
    const bool managed=particle.elapsed_seconds>=0;
    if(managed?particle.elapsed_seconds>=double(life)*kVehicleStepSeconds:elapsed_steps>=life_steps)return false;
    const double age=managed?particle.elapsed_seconds:(particle.solver_dt==kVehicleStepSeconds?elapsed_steps/120.0:
        elapsed_steps*particle.solver_dt);
    const double fraction=managed?age/(double(life)*kVehicleStepSeconds):elapsed_steps/life_steps;
    position = {particle.origin.x+particle.velocity.x*age,
                particle.origin.y+particle.velocity.y*age,
                particle.origin.z+particle.velocity.z*age};
    if (particle.kind == RaceParticleKind::smoke) {
        diameter = particle.size*(1.0+2.2*fraction);
        opacity = 0.70*(1.0-fraction);
    } else {
        diameter = particle.size*(1.0-0.55*fraction);
        opacity = 0.95*(1.0-fraction);
    }
    return true;
}

bool RaceParticles::visual(const RaceParticle& p, VehicleVec3& position, double& diameter,
                           double& opacity, std::array<unsigned,3>& rgb) const noexcept {
    if (!p.life_ticks) {
        rgb = {255,255,255};
        return race_particle_visual(p,step_,position,diameter,opacity);
    }
    if (step_ < p.born_step || particle_age(p,step_) >= particle_life(p)) return false;
    const auto& preset = preset_for(p);
    const auto remaining = p.life_ticks-static_cast<unsigned>(
        particle_age(p,step_)*kParticleTicksPerSecond);
    const double phase = 1.0-double(remaining)/(kParticleTicksPerSecond*preset.life);
    const auto cubic = [phase](double a,double b,double c,double d) {
        return ((a*phase+b)*phase+c)*phase+d;
    };
    position = add(p.origin,p.displacement);
    diameter = cubic(preset.size[0],preset.size[1],preset.size[2],preset.size[3]);
    for (unsigned c=0; c<4; ++c) {
        const double value = std::clamp(cubic(preset.rgba[0][c],preset.rgba[1][c],
            preset.rgba[2][c],preset.rgba[3][c]),0.0,255.0);
        if (c < 3) rgb[c] = static_cast<unsigned>(value);
        else opacity = static_cast<unsigned>(value*.5)/255.0;
    }
    return diameter > 0;
}

bool decode_race_particle_textures(const std::vector<unsigned char>& bytes,
                                   const TexturePackSet& packs, RaceParticleTextures& textures,
                                   std::string& error, bool environment) {
    std::vector<std::uint32_t> wanted;
    const auto count=environment?kRaceParticleAtlases.size():kRaceVehicleAtlasCount;
    for (std::size_t i=0;i<count;++i) wanted.push_back(kRaceParticleAtlases[i].hash);
    std::sort(wanted.begin(),wanted.end());
    RenderTextureSource source;
    source.bytes = &bytes;
    source.wanted = &wanted;
    source.complete = packs.complete;
    for (const auto& pack : packs.packs) source.packs.push_back(&pack);
    RenderTextureSet decoded;
    RenderTextureLimits limits;
    limits.max_total_bytes = 0;
    for (std::size_t i=0;i<count;++i) {
        const auto& atlas=kRaceParticleAtlases[i];
        limits.max_total_bytes += atlas.width*atlas.height*4;
    }
    if (!decode_render_texture_sources({source}, limits, decoded, error)) return false;
    RaceParticleTextures result;
    for (std::size_t i=0;i<count;++i) {
        const auto& expected = kRaceParticleAtlases[i];
        const auto texture = std::find_if(decoded.textures.begin(),decoded.textures.end(),
            [&](const auto& t) { return t.hash == expected.hash; });
        if (texture == decoded.textures.end() || !texture->usable() ||
            texture->width != expected.width || texture->height != expected.height) {
            error = std::string("INGAMEB.BUN is missing particle atlas ") + expected.name;
            return false;
        }
        result[i] = std::move(*texture);
    }
    textures = std::move(result);
    return true;
}

unsigned race_particle_atlas_cell(const RaceParticle& particle, std::uint64_t step) noexcept {
    const auto& atlas = kRaceParticleAtlases[static_cast<unsigned>(particle.atlas)];
    const auto period = particle.life_ticks ? particle.frame_period :
        particle.kind == RaceParticleKind::spark ? kParticleTicksPerSecond/22 : 0;
    unsigned frames = 0;
    if (period && step >= particle.born_step) {
        const double life = particle_life(particle);
        const auto initial = static_cast<unsigned>(life*kParticleTicksPerSecond);
        const auto elapsed = static_cast<unsigned>(std::clamp(particle_age(particle,step),0.0,life)*
            kParticleTicksPerSecond);
        frames = initial/period-(initial-elapsed)/period;
    }
    return atlas.first_cell+(particle.initial_cell+frames)%(atlas.columns*atlas.rows);
}

std::array<std::uint8_t,4> race_particle_texel(RaceParticleKind kind,
                                                unsigned x, unsigned y) noexcept {
    const int dx = static_cast<int>(x)*2-15;
    const int dy = static_cast<int>(y)*2-15;
    const int radius = dx*dx+dy*dy;
    if (kind == RaceParticleKind::smoke) {
        const int alpha = std::max(0, 210-radius);
        return {210,208,205,static_cast<std::uint8_t>(alpha*175/210)};
    }
    const int alpha = std::max(0, 110-radius);
    return {255,214,112,static_cast<std::uint8_t>(alpha*255/110)};
}

bool draw_race_particles(std::vector<unsigned char>& rgba,
                         const RenderOptions& options,
                         const RaceParticles& particles,
                         std::string& error,
                         const Frame* track,
                         const Frame* car,
                         const RaceParticleTextures* textures) {
    if (rgba.size() != static_cast<std::uint64_t>(options.width)*options.height*4) {
        error="particle frame size differs from the render options";
        return false;
    }
    if ((track || car) && (!track || !car ||
        track->width()!=options.width || track->height()!=options.height ||
        car->width()!=options.width || car->height()!=options.height)) {
        error="particle depth frames differ from the render options";
        return false;
    }
    const auto atlas_count=particles.environment_emitter_count()?kRaceParticleAtlases.size():kRaceVehicleAtlasCount;
    if (textures && std::any_of(textures->begin(),textures->begin()+atlas_count,
                               [](const auto& atlas) { return !atlas.usable(); })) {
        error = "particle atlas payload is invalid";
        return false;
    }
    CameraFrame camera;
    if (!make_camera_frame(options.camera,options.width,options.height,camera,error)) return false;
    const auto draw = [&](const RaceParticle& particle) {
        VehicleVec3 at;
        double diameter=0, opacity=0;
        std::array<unsigned,3> tint;
        if (!particles.visual(particle,at,diameter,opacity,tint) || opacity <= 0) return;
        const ClipVertex clip=project_point(camera,{at.x,at.z,-at.y});
        if (clip.w <= camera.near_plane || clip.w > camera.far_plane) return;
        const double cx=(clip.x/clip.w+1.0)*options.width*0.5;
        const double cy=(1.0-clip.y/clip.w)*options.height*0.5;
        const double pixels=std::min(diameter*camera.focal_y*options.height/(2.0*clip.w),
                                     48.0*options.height/720.0);
        if (pixels < 0.5) return;
        const double half=pixels*0.5;
        const int left=std::max(0,static_cast<int>(std::floor(cx-half)));
        const int right=std::min(static_cast<int>(options.width),
                                 static_cast<int>(std::ceil(cx+half)));
        const int top=std::max(0,static_cast<int>(std::floor(cy-half)));
        const int bottom=std::min(static_cast<int>(options.height),
                                  static_cast<int>(std::ceil(cy+half)));
        const bool smoke = particle.kind == RaceParticleKind::smoke;
        const auto atlas_index = static_cast<unsigned>(particle.atlas);
        const auto& layout = kRaceParticleAtlases[atlas_index];
        const RenderTexture* atlas = textures ? &(*textures)[atlas_index] : nullptr;
        const unsigned cell = race_particle_atlas_cell(particle,particles.step())-layout.first_cell;
        const int width = atlas ? int(atlas->width / layout.columns) : 16;
        const int height = atlas ? int(atlas->height / layout.rows) : 16;
        const auto cell_x = (cell % layout.columns) * width;
        const auto cell_y = (cell / layout.columns) * height;
        for (int y=top; y<bottom; ++y) for (int x=left; x<right; ++x) {
            const auto tx = static_cast<unsigned>(std::clamp(
                static_cast<int>(width * ((x + 0.5 - cx) / pixels + 0.5)), 0, width - 1));
            const auto ty = static_cast<unsigned>(std::clamp(
                static_cast<int>(height * ((y + 0.5 - cy) / pixels + 0.5)), 0, height - 1));
            std::array<std::uint8_t,4> texel;
            if (atlas) {
                const auto offset = (std::size_t(cell_y + ty) * atlas->width + cell_x + tx) * 4;
                std::copy_n(atlas->rgba.data() + offset, texel.size(), texel.begin());
            } else {
                texel = race_particle_texel(particle.kind, tx, ty);
            }
            const int alpha=static_cast<int>(texel[3]*opacity+0.5);
            if (!alpha) continue;
            const auto index=static_cast<std::size_t>(y)*options.width+x;
            if (track) {
                double scene_depth=track->depth()[index];
                if (car->ids()[index]!=kNoTriangle && car->depth()[index]<=scene_depth)
                    scene_depth=car->depth()[index];
                if (clip.z/clip.w > scene_depth) continue;
            }
            const auto pixel=index*4;
            for (unsigned channel=0; channel<3; ++channel) {
                const int source = (texel[channel]*tint[channel]+127)/255;
                const int value=smoke
                    ? (source*alpha+rgba[pixel+channel]*(255-alpha)+127)/255
                    : std::min(255,static_cast<int>(rgba[pixel+channel])+
                                   (source*alpha+127)/255);
                rgba[pixel+channel]=static_cast<unsigned char>(value);
            }
        }
    };
    for (std::size_t i=0; i<particles.smoke_count(); ++i) draw(particles.smoke(i));
    for (std::size_t i=0; i<particles.spark_count(); ++i) draw(particles.spark(i));
    for (std::size_t i=0;i<particles.environment_alpha_count();++i) draw(particles.environment_alpha(i));
    for (std::size_t i=0;i<particles.environment_additive_count();++i) draw(particles.environment_additive(i));
    error.clear();
    return true;
}
} // namespace nfsu
