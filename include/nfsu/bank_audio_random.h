#pragma once
#include <array>
#include <cstdint>
namespace nfsu::audio {
// Original651150/6511B0. This is the SND bank-callback generator, separate
// from high-level sound6F227C and the simulation/world generator.
class BankAudioRandom {
public:
    explicit BankAudioRandom(std::uint32_t value=0)noexcept {seed(value);}
    void seed(std::uint32_t value)noexcept {
        constexpr std::array<std::uint32_t,6> initial{{0xf22d0e56U,0x883126e9U,
            0xc624dd2fU,0x0702c49cU,0x9e353f7dU,0x6fdf3b64U}};
        for(unsigned i=0;i<state_.size();++i)state_[i]=initial[i]+value;
    }
    const std::array<std::uint32_t,6>& state()const noexcept {return state_;}
    void restore(const std::array<std::uint32_t,6>& value)noexcept {state_=value;}
    std::uint32_t next()noexcept {
        const auto a=state_[4]+state_[5];
        const auto b=(a+std::uint32_t(a<state_[4]||a<state_[5]))+state_[3];
        const auto c=(b+std::uint32_t(b<state_[3]))+state_[2];
        const auto d=(c+std::uint32_t(c<state_[2]))+state_[1];
        state_[0]+=(d+std::uint32_t(d<state_[1]));
        state_[1]=d;state_[2]=c;state_[3]=b;state_[4]=a;
        if(++state_[5]==0 && ++state_[4]==0 && ++state_[3]==0 &&
           ++state_[2]==0 && ++state_[1]==0)++state_[0];
        return state_[0];
    }
private:
    std::array<std::uint32_t,6> state_{};
};
// Fresh SND configuration seed0, retained across module/actor/race lifetimes.
// All callback consumers share this state unless a replay supplies a context.
inline BankAudioRandom& process_bank_audio_random()noexcept {
    static BankAudioRandom random;return random;
}
} //namespace nfsu::audio
