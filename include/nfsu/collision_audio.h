#pragma once

#include "nfsu/vehicle_track.h"
#include <array>
#include <cstdint>
#include <vector>

namespace nfsu::audio {

// Typed copied input. Kind values match retail body +0x15. There is no
// simulated actor pointer, simulation RNG, or mutable state reference.
struct CollisionContact {
    std::uint64_t step = 0;
    std::uint32_t pair = 0;
    VehicleVec3 impulse;
    VehicleVec3 forward{1, 0, 0}, up{0, 0, 1};
    double slide_mps = 0;
    std::uint8_t other_kind = 3;
};

// Curves are read from the identified retail banks, never bundled PCM.
struct CollisionGraph {
    std::array<std::int16_t, 128> impact_gain{}, impact_pitch{};
    std::array<std::int16_t, 128> scrape_gain{}, scrape_pitch{};
    std::array<std::int16_t, 10> scrape_pitch_targets{}, scrape_gain_targets{};
};
CollisionGraph collision_graph(const std::vector<std::uint8_t>& impact,
                               const std::vector<std::uint8_t>& scrape);
int world_collision_subtype(const CollisionContact& contact) noexcept;
int collision_intensity(double impulse_t_m_s, int subtype) noexcept;

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

struct CollisionVoice {
    std::uint32_t pair = 0, sample = 0, gain_q15 = 0, pitch_q16 = 65536;
    bool start = false;
};
struct CollisionAudioFrame {
    std::vector<CollisionVoice> impacts, scrapes;
    bool processed = false;
};

// Retail selectors and bank curves; explicitly port-side contact episodes,
// two output scrape voices, graph update at 60 Hz, and private RNG seed/order.
// Episodes reset after a gap in observed 120 Hz polygon contact. This is NOT
// a recovered retail body-pair lifetime or global admission algorithm.
class CollisionScheduler {
public:
    // tick identifies an audio frame; physics_step ages contacts even when
    // the frame contains none. Neither clock is derived from the other.
    CollisionAudioFrame update(std::uint64_t tick, std::uint64_t physics_step,
                               const CollisionContact* contacts,
                               std::size_t count, const CollisionGraph& graph,
                               bool reset = false);
    void clear() noexcept;
private:
    struct Pair {
        std::uint32_t id = 0;
        std::uint64_t last_step = 0;
        double sum = 0;
        unsigned variant = 0; int subtype = -1, jitter = 0;
        bool impacting = false;
        float pitch_phase = 0, gain_phase = 0;
        float pitch_value = 0, gain_value = 0;
        float pitch_delta = 0, gain_delta = 0;
        int pitch_target = 0, gain_target = 0, pitch_last = 0, gain_last = 0;
        bool pitch_high = false, gain_high = false, used = false, scraping = false;
        CollisionDeck pitch_deck, gain_deck;
    };
    std::array<Pair, 256> pairs_{};
    unsigned family_four_ = 0, family_land_ = 0, family_three_ = 0;
    std::uint32_t rng_ = 0x2225A17U;
    std::uint64_t last_tick_ = 0;
    std::uint64_t last_physics_step_ = 0;
    bool have_tick_ = false;
    unsigned random(unsigned range) noexcept;
};

} // namespace nfsu::audio
