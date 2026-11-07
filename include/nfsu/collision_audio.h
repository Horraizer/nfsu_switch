#pragma once

#include "nfsu/vehicle_track.h"
#include "nfsu/race_limits.h"
#include "nfsu/bank_audio_random.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nfsu::audio {

// Port input bound: four solver slices of256 hull contacts plus the complete
// player/other-car roster, including traffic.
// This is a memory/work limit, not a recovered retail admission policy.
constexpr std::size_t kCollisionContactFrameLimit = 4*256+kRaceRivalCapacity+kRaceTrafficCapacity;

// Typed copied input. Kind values match retail body +0x15. There is no
// simulated actor pointer, simulation RNG, or mutable state reference.
struct CollisionContact {
    std::uint64_t step = 0;
    std::uint32_t pair = 0;
    VehicleVec3 impulse;
    VehicleVec3 forward{1, 0, 0}, up{0, 0, 1};
    double slide_mps = 0;
    std::uint8_t other_kind = 3;
    int car_subtype = 0;
    VehicleVec3 point{};bool point_valid=false; // copied physical witness, never body-center fallback
    std::uint32_t audio_order=0;
};

// Curves are read from the identified retail banks, never bundled PCM.
struct CollisionGraph {
    std::array<std::int16_t, 128> impact_gain{}, impact_pitch{};
    std::array<std::int16_t, 128> scrape_gain{}, scrape_pitch{};
    std::array<std::int16_t, 10> scrape_pitch_targets{}, scrape_gain_targets{};
};
CollisionGraph collision_graph(const std::vector<std::uint8_t>& impact,
                               const std::vector<std::uint8_t>& scrape);
int car_collision_subtype(VehicleVec3 position_a,VehicleVec3 position_b,
    VehicleVec3 velocity_a,VehicleVec3 velocity_b,VehicleVec3 point) noexcept;
int world_collision_subtype(const CollisionContact& contact) noexcept;
int collision_intensity(double impulse_t_m_s, int subtype) noexcept;
int collision_scrape_intensity(double slide_mps) noexcept;
//531670/595170: the smokeable controller maps its own change in velocity.
int prop_collision_intensity(float impulse,float inverse_mass) noexcept;
unsigned prop_collision_variant(unsigned material,std::array<unsigned,29>& counters) noexcept;

// Native callback 8's ten-entry bag, including no immediate repeat at wrap.
class CollisionDeck {
public:
    CollisionDeck() noexcept;
    unsigned next(std::uint32_t random) noexcept;
private:
    std::array<std::uint8_t,10> deck_{};
    unsigned index_ = 0;
    bool cycled_ = false;
};

struct CollisionSpatialFrame {
    std::array<float,3> actor{},eye{},forward{{1,0,0}};
    int impact_global_gain=32768; // supplied5365E0 publication; scrape omits this stage
};
struct CollisionVoice {
    std::uint32_t pair = 0, sample = 0, gain_q15 = 0, pitch_q16 = 65536;
    bool start = false;
    std::uint16_t pan=0;bool positional=false;
    std::uint64_t episode=0;float admission_distance=0;int base_pitch=4096;
    std::uint32_t audio_order=0;
};
struct CollisionAdmissionRecord {
    std::uint32_t id=0,actor=0;int subtype=5;
    bool attached=true,started=false,rejected=false;
    std::array<float,3> direction{},point{};
};
bool collision_world_admitted(const CollisionAdmissionRecord& current,
    const std::array<float,3>& previous_direction,const std::array<float,3>& previous_point,
    const CollisionAdmissionRecord* attached,std::size_t count) noexcept;
//52FB60/6F7E94: queue admission, separate from record admission and backend voices.
struct CollisionQueueRecord {std::uint32_t id=0;bool priority=false;float distance=0;};
struct CollisionQueueCommand {bool start=false;std::uint32_t id=0;};
constexpr unsigned kCollisionImpactCapacity=5;
std::vector<CollisionQueueCommand> collision_impact_queue(const std::vector<CollisionQueueRecord>& pending,
    const std::vector<CollisionQueueRecord>& attached,unsigned capacity=kCollisionImpactCapacity,unsigned active_count=UINT32_MAX);

struct CollisionAudioFrame {
    std::vector<CollisionVoice> impacts, scrapes;
    bool processed = false;
};

// Retail selectors and bank curves; explicitly port-side contact episodes,
// two output scrape voices and graph update at 60 Hz. Bank callbacks consume
// shared6511B0 state; natural callback caller order remains separate.
// Retail300 ms last-touch lifetime and133 ms release. Track pair identity still
// uses copied polygons; this is not original body-pair/global admission parity.
class CollisionScheduler {
public:
    // tick identifies an audio frame; physics_step ages contacts even when
    // the frame contains none. Neither clock is derived from the other.
    CollisionAudioFrame update(std::uint64_t tick, std::uint64_t physics_step,
                               const CollisionContact* contacts,
                               std::size_t count, const CollisionGraph& graph,
                               bool reset = false,int category_gain = 32768,const CollisionSpatialFrame* spatial=nullptr,bool defer_impact_start=false);
    bool start_impact(CollisionVoice& voice);
    void clear() noexcept;
    void set_random_context(BankAudioRandom* random)noexcept {random_context_=random;}
private:
    struct Pair {
        std::uint32_t id = 0;
        std::uint64_t last_step = 0, impact_step = 0, episode=0;
        bool started=false,rejected=false;
        std::array<float,3> direction{},attached_direction{};
        std::uint64_t output_tick=0,output_step=0;std::size_t output_index=0;
        double sum = 0;
        double slide = 0;
        unsigned variant = 0; int subtype = -1, jitter = 0;
        bool impacting = false;
        std::array<float,3> point{};bool point_valid=false;
        float pitch_phase = 0, gain_phase = 0;
        float pitch_value = 0, gain_value = 0;
        float pitch_delta = 0, gain_delta = 0;
        int pitch_target = 0, gain_target = 0, pitch_last = 0, gain_last = 0;
        bool pitch_high = false, gain_high = false, used = false, scraping = false;
        CollisionDeck pitch_deck, gain_deck;
    };
    std::array<Pair,256+kRaceRivalCapacity+kRaceTrafficCapacity> pairs_{};
    unsigned family_four_ = 0, family_land_ = 0, family_three_ = 0;
    unsigned car_four_ = 0, car_three_ = 0;
    BankAudioRandom* random_context_=nullptr;
    std::uint64_t last_tick_ = 0;
    std::uint64_t last_physics_step_ = 0;
    bool have_tick_ = false;
    std::array<float,3> previous_direction_{},previous_point_{};
    std::uint32_t random_word() noexcept;
};

} // namespace nfsu::audio
