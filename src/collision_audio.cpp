#include "nfsu/collision_audio.h"
#include "nfsu/sha256.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace nfsu::audio {
namespace {
std::uint32_t u32(const std::vector<std::uint8_t>& b, std::size_t p) {
    if (p > b.size() || b.size() - p < 4) throw std::runtime_error("collision graph truncated");
    return b[p] | (std::uint32_t(b[p+1]) << 8) |
           (std::uint32_t(b[p+2]) << 16) | (std::uint32_t(b[p+3]) << 24);
}
template<std::size_t N>
void curve(const std::vector<std::uint8_t>& b, std::size_t p,
           std::array<std::int16_t, N>& out) {
    if (u32(b, p) != (std::uint32_t(N) << 16 | 2U) || u32(b,p+4) != 0 ||
        u32(b,p+8) != N-1 || u32(b,p+12) != 0x3F800000U)
        throw std::runtime_error("collision graph curve header mismatch");
    for (std::size_t i = 0; i < N; ++i) {
        if (p+16+2*i+1 >= b.size()) throw std::runtime_error("collision graph curve truncated");
        const unsigned v = b[p+16+2*i] | (unsigned(b[p+17+2*i]) << 8);
        out[i] = static_cast<std::int16_t>(v < 32768 ? int(v) : int(v)-65536);
    }
}
// Retail FIST nearest-even conversion, without depending on host rounding mode.
int nearest(double x) noexcept {
    const double base = std::floor(x), part = x-base;
    const int n = static_cast<int>(base);
    return n + (part > 0.5 || (part == 0.5 && (n & 1)) ? 1 : 0);
}
int intensity(double x, double upper) noexcept {
    if (!std::isfinite(x) || x <= 0) return 0;
    return nearest(std::min(x/upper, 1.0)*127);
}
std::uint32_t pitch(int q12) noexcept {
    return static_cast<std::uint32_t>(std::clamp(q12, 1, 9000))*16;
}
double length(VehicleVec3 v) noexcept { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); }
}

CollisionGraph collision_graph(const std::vector<std::uint8_t>& impact,
                               const std::vector<std::uint8_t>& scrape) {
    if (sha256_hex(sha256(impact.data(), impact.size())) !=
        "C1EE8237C292E2934E296CAF10E7A78B25BDAE8326C517D1AA034379362CC6D5" ||
        sha256_hex(sha256(scrape.data(), scrape.size())) !=
        "F8064C70C59BB0B6E61596DE7F1A6856EA4AC48ACE3A48D50849C2B4E9FECB50")
        throw std::runtime_error("unsupported collision bank SHA256");
    if (u32(impact,0x66C) != 13 || u32(scrape,0x37C) != 1 || u32(scrape,0x384) != 1)
        throw std::runtime_error("collision Player table mismatch");
    for (unsigned i = 0; i < 13; ++i)
        if (u32(impact,0x674+12*i) != 30+i)
            throw std::runtime_error("world impact sample binding mismatch");
    CollisionGraph g;
    curve(impact,0xA3C,g.impact_gain); curve(impact,0xB4C,g.impact_pitch);
    curve(scrape,0x38C,g.scrape_gain); curve(scrape,0x49C,g.scrape_pitch);
    curve(scrape,0x5AC,g.scrape_pitch_targets); curve(scrape,0x5D0,g.scrape_gain_targets);
    return g;
}

int world_collision_subtype(const CollisionContact& c) noexcept {
    // 0x0052F6F0: kind 1 is smokeable; kind 0 is car (not world).
    if (c.other_kind != 3 && c.other_kind != 4) return -1;
    const double n = length(c.impulse);
    if (!std::isfinite(n) || n <= 0 || !std::isfinite(c.up.z)) return -1;
    const VehicleVec3 j{c.impulse.x/n,c.impulse.y/n,c.impulse.z/n};
    constexpr double vertical = double(0.7070000171661377F);
    if (std::abs(j.z) > vertical) {
        if (-c.up.z > double(-0.10000000149011612F)) return 2;
        return c.up.z > vertical ? 3 : 4;
    }
    const double along = (c.forward.x*j.x+c.forward.y*j.y)+c.forward.z*j.z;
    if (!std::isfinite(along)) return -1;
    return std::abs(along) > 0.707 ? 4 : 5;
}

int collision_intensity(double impulse, int subtype) noexcept {
    constexpr double multiplier[]{12,1,1,1,17,13,1}; // 0x006B6450
    return subtype >= 0 && subtype < 7 ? intensity(impulse,3*multiplier[subtype]) : 0;
}

CollisionDeck::CollisionDeck() noexcept {
    for (unsigned i=0;i<10;++i) deck_[i]=std::uint8_t(i);
}
unsigned CollisionDeck::next(std::uint32_t random) noexcept {
    const unsigned pick=index_+random%(10-index_-(index_==0 && cycled_ ? 1U : 0U));
    const unsigned value=deck_[pick]; std::swap(deck_[index_],deck_[pick]);
    if (++index_==10) { index_=0; cycled_=true; }
    return value;
}

unsigned CollisionScheduler::random(unsigned range) noexcept {
    // Private output-only RNG. Bounds/deck rule are retail; seed/sequence are
    // port policy pending original runtime evidence. Never use simulation RNG.
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return range ? rng_%range : 0;
}
void CollisionScheduler::clear() noexcept {
    pairs_ = {}; family_four_ = family_land_ = family_three_ = 0;
    rng_ = 0x2225A17U; have_tick_ = false; last_tick_ = last_physics_step_ = 0;
}

CollisionAudioFrame CollisionScheduler::update(std::uint64_t tick, std::uint64_t physics_step,
    const CollisionContact* contacts, std::size_t count, const CollisionGraph& g, bool reset) {
    CollisionAudioFrame out;
    if (have_tick_ && tick == last_tick_) return out;
    if (reset || (have_tick_ && (tick < last_tick_ || physics_step < last_physics_step_))) clear();
    // Countdown and held physics do not advance contact episodes or graph
    // controls. Remember the race tick so a repeated reset is also idempotent.
    const bool held = have_tick_ && physics_step == last_physics_step_;
    have_tick_ = true; last_tick_ = tick; last_physics_step_ = physics_step;
    if (held) return out;
    out.processed = true;
    count = std::min(count,std::size_t(512));
    if (!contacts) count = 0;
    // A reset snapshot discards pre-reset contacts from the same tick.
    if (reset) count = 0;
    const std::uint64_t newest = physics_step;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& c = contacts[i];
        if ((c.other_kind != 3 && c.other_kind != 4) || !c.step || c.step > newest) continue;
        Pair* pair = nullptr;
        for (auto& p : pairs_) if (p.used && p.id == c.pair) { pair = &p; break; }
        if (!pair) for (auto& p : pairs_) if (!p.used || newest > p.last_step+2) {
            p = {}; p.used = true; p.id = c.pair; pair = &p; break;
        }
        if (!pair || c.step <= pair->last_step) continue;
        const bool begin = !pair->last_step || c.step > pair->last_step+1;
        if (begin) {
            const auto id = pair->id;
            *pair = {}; pair->used = true; pair->id = id;
            pair->pitch_target=g.scrape_pitch_targets[0]; pair->gain_target=g.scrape_gain_targets[0];
        }
        pair->last_step = c.step;
        const double mag = length(c.impulse);
        if (std::isfinite(mag)) pair->sum += mag;
        const int subtype = world_collision_subtype(c);
        if (subtype >= 0 && mag > 0) {
            const bool impact_start = !pair->impacting;
            if (impact_start) {
            unsigned variant = 0;
            if (subtype == 2 || subtype == 5 || subtype == 6) {
                family_four_ = (family_four_+1)%4; variant = 5+family_four_;
            } else if (subtype == 3) {
                family_land_ = (family_land_+1)%3; variant = family_land_;
            } else if (subtype == 4) {
                family_three_ = (family_three_+1)%3; variant = 9+family_three_;
            }
            pair->variant = variant; pair->subtype = subtype;
            pair->jitter = int(random(301))-150; pair->impacting = true;
            }
            const int ix = collision_intensity(pair->sum,pair->subtype);
            constexpr int gain[]{32767,22500,22000,20000,32767,22000,20000};
            const int volume = nearest(double(std::max(0,int(g.impact_gain[ix])))*gain[pair->subtype]/32768);
            out.impacts.push_back({c.pair,30+pair->variant,std::uint32_t(volume),
                pitch(4096+g.impact_pitch[ix]+pair->jitter),impact_start});
        }
    }
    // Last observation for each pair drives the continuous graph once per
    // audio tick; physics slices do not advance oscillators separately.
    for (auto& p : pairs_) {
        if (!p.used || newest > p.last_step+1) { p.used = false; continue; }
        const CollisionContact* c = nullptr;
        for (std::size_t i = count; i > 0; --i)
            if (contacts[i-1].pair == p.id && contacts[i-1].step == p.last_step) { c=&contacts[i-1]; break; }
        if (!c || !std::isfinite(c->slide_mps) || !(c->slide_mps > 0)) { p.scraping=false; continue; }
        const bool start = !p.scraping; p.scraping = true;
        // Square oscillator callbacks 28 / rising-edge 10, periods 250 and
        // 2000 ms; deck callback 8. Port control cadence is 60 Hz.
        const auto oscillator = [](float& phase, bool& high, float period) {
            while (phase >= 1) phase -= 1;
            const bool now = phase >= 0.5F, rising = now && !high;
            high = now; phase += (1000.0F/60)/period; return rising;
        };
        const auto ramp = [](float& value, float& delta, int& old, int target, float duration) {
            if (target != old) { old=target; delta=(float(target)-value)*(1000.0F/60)/duration; }
            value += delta;
            if ((delta >= 0 && value > float(target)) || (delta < 0 && value < float(target))) value=float(target);
            return nearest(value);
        };
        if (oscillator(p.pitch_phase,p.pitch_high,250))
            p.pitch_target=g.scrape_pitch_targets[p.pitch_deck.next(random(UINT32_MAX))];
        if (oscillator(p.gain_phase,p.gain_high,2000))
            p.gain_target=g.scrape_gain_targets[p.gain_deck.next(random(UINT32_MAX))];
        // Before the first rising edge, deck output is index zero.
        const int pitch_mod = ramp(p.pitch_value,p.pitch_delta,p.pitch_last,p.pitch_target,261);
        const int gain_mod = ramp(p.gain_value,p.gain_delta,p.gain_last,p.gain_target,370);
        const int ix = intensity(c->slide_mps,40);
        const int base = nearest(double(std::max(0,int(g.scrape_gain[ix])))*20000/32768);
        // Template float factor 0x38000100, close to 1/32767.
        const int volume = nearest(double(base)*std::clamp(gain_mod,0,32767)*double(0x1.0002p-15F));
        out.scrapes.push_back({p.id,1,std::uint32_t(volume),pitch(g.scrape_pitch[ix]+pitch_mod),start});
    }
    return out;
}
} // namespace nfsu::audio
