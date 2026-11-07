#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include "nfsu/remote_engine_audio.h"
#include "nfsu/bank_audio_random.h"

namespace nfsu::audio {

struct EngineAudioSelection {
    unsigned group = 0, upgrade = 0;
    std::string engine, exhaust;
};
// Original 520320/6F8100 and 521C80. Upgrade is the engine level in
// the selected car descriptor (+20), not the visual body-kit level.
EngineAudioSelection engine_audio_selection(const std::string& car, int upgrade);

//523DDD/523DEB: bank RPM is normalized against the tuned RideInfo maximum,
//not physical RPM. The fixed reference is10000. Invalid port inputs return0.
float engine_audio_rpm(float physical_rpm,float maximum_rpm)noexcept;
std::array<std::uint32_t,2> engine_audio_parameters(float rpm,float load) noexcept;

// Remote sound control path523990/523A70/523B30/523C30/523E30/527180.
// Physical RPM/torque remain independent of these audible shift envelopes.
struct EngineEnvelopeTuning {
    int up_offset=0,up_ms=0,up_extra_ms=0;
    float up_extra=0;
    int down_rise=0,down_rise_ms=0,down_dip=0,down_dip_ms=0;
    float catch_ms_per_rpm=0;
    int human_up_drop=0,human_up_drop_ms=0,human_down_drop=0,human_down_drop_ms=0;
    float human_shift_up_delay=0,human_shift_down_delay=0;
    int human_shift_gain=0;
    float profile_rise_factor=0,profile_shift_drop=0,profile_gain_offset=0,profile_balance_boost=0;
    int profile_fall_ms=0;
};
EngineEnvelopeTuning engine_envelope_tuning(const std::uint8_t*,std::size_t);
struct EngineEnvelopeInput {
    float rpm=0,maximum_rpm=10000,drive_load=0,throttle=0,brake=0;
    float load_divisor=160.F; // Remote5234C0; human supplies526420 selected torque maximum.
    bool handbrake=false;
    int gear=0,game_state=4;
    float dt=1.0F/60.0F;
    float gear_ratio=1,first_gear_ratio=1; // Selected RideInfo+1D0/+1D8.
};
struct EngineEnvelopeOutput {float rpm=0,load=0;unsigned shift_state=0;float gain_boost=0;
    unsigned shift_cue=0;std::int32_t shift_gain=0;unsigned disengage=0;
    float adaptive_blend=0;int adaptive_offset=0;float adaptive_balance_boost=0;};
//5235A0: selected sound gains, upshift accent and authored channel balance.
std::array<unsigned,2> engine_audio_gains(int,int,int,float) noexcept;
std::array<std::int32_t,2> remote_engine_audio_gains(int,int,int,float,
    float adaptive_blend=0,int adaptive_offset=0,float adaptive_balance_boost=0)noexcept;
struct RemoteEngineContext {
    RemoteRevScript* script=nullptr;
    TrafficAudioRandom* sound_random=nullptr;
    TrafficAudioRandom* world_random=nullptr;
    float countdown_seconds=0;
};
struct EngineGainProfileState {
    float time=0,last_off=0,blend=0;int offset=0;unsigned mode=0;bool throttle_open=false;
};
class EngineEnvelope {
    friend class HumanEngineEnvelope;
public:
    void configure(const EngineEnvelopeTuning& tuning) noexcept {tuning_=tuning;reset();}
    void reset() noexcept;
    EngineEnvelopeOutput tick(const EngineEnvelopeInput&,RemoteEngineContext* = nullptr) noexcept;
    EngineGainProfileState profile_state()const noexcept {
        return {time_,last_off_,profile_.blend,profile_.offset,profile_.mode,on_};
    }
private:
    struct Curve {
        float elapsed=0,duration=0,from=0,to=0,value=0;
        unsigned mode=0;
        bool done=true;
        void start(float,float,int,unsigned=0) noexcept;
        void advance(float) noexcept;
    };
    struct Average {
        std::array<float,10> values{};
        unsigned cursor=0,count=0;
        float sum=0,value=0;
        void fill(float) noexcept;
        float push(float) noexcept;
    };
    // Original529EA0 and shift completion hooks; the RPM subtraction owner
    // at196C is separate. This profile publishes only gain/balance controls.
    struct GainProfile {
        Curve curve;
        unsigned mode=0;
        float blend=0;
        int offset=0;
        void rise(float,const EngineEnvelopeTuning&,const EngineEnvelopeInput&) noexcept;
        void up_done(float,int)noexcept;
        void down_done()noexcept;
        void tick(const EngineEnvelopeTuning&,const EngineEnvelopeInput&,float,float,float,bool)noexcept;
    };
    void input(const EngineEnvelopeInput&) noexcept;
    EngineEnvelopeTuning tuning_;
    Curve rpm_curve_,load_curve_,gain_curve_;
    Average average_;
    GainProfile profile_;
    float time_=0,last_off_=0;
    float rpm_=0,physical_=0,torque_=0,load_=0,on_timer_=0,off_timer_=0,shift_rpm_=0;
    int gear_=0;
    unsigned shift_=0;
    bool initialized_=false,on_=false;
};

struct HumanEngineProfileState {
    float time=0,throttle_on=0,throttle_off=0,last_shift=0,audible_rpm=0;
    int gear=0;bool throttle_open=false;
};
// Pending5292B0/5293E0 ->529530 notification state. This consumes a real
// gear notification and the preceding audible RPM/time publication.
struct HumanEngineShiftCue {
    float due=0;unsigned kind=0;bool pending=false;
    void notify(bool up,int gear,float rpm,float time,float up_delay,float down_delay) noexcept {
        if((up&&rpm<3000.F)||(!up&&gear==1))return;
        pending=true;kind=up?1U:2U;due=time+(up?up_delay:down_delay);
    }
    unsigned tick(float time) noexcept {
        if(!pending||!(due<time))return 0;
        pending=false;return kind;
    }
};
// Original529100 gain before the human position/category consumer and bank.
std::int32_t human_shift_audio_gain(std::int16_t tuning_gain,float audible_rpm)noexcept;
// Human52C620 dispatch. The remote524620 envelope above is a different actor.
class HumanEngineEnvelope {
public:
    void configure(const EngineEnvelopeTuning& tuning)noexcept {tuning_=tuning;reset();}
    void reset()noexcept;
    EngineEnvelopeOutput tick(const EngineEnvelopeInput&)noexcept;
    HumanEngineProfileState profile_state()const noexcept {return {time_,on_time_,off_time_,shift_time_,rpm_,gear_,throttle_on_};}
    EngineGainProfileState gain_profile_state()const noexcept {
        return {time_,off_time_,profile_.blend,profile_.offset,profile_.mode,throttle_on_};
    }
private:
    EngineEnvelopeTuning tuning_;
    HumanEngineShiftCue shift_cue_;
    EngineEnvelope::Curve rpm_curve_,load_curve_,gain_curve_;
    EngineEnvelope::Average average_;
    EngineEnvelope::GainProfile profile_;
    float rpm_=0,load_=0,time_=0,on_time_=0,off_time_=0,shift_time_=0,shift_rpm_=0;
    int gear_=0;unsigned shift_=0;bool initialized_=false,throttle_on_=false;
};
std::array<unsigned,2> human_engine_audio_gains(int,int,int,float,
    float adaptive_blend=0,int adaptive_offset=0,float adaptive_balance_boost=0,unsigned camera_pov=0)noexcept;
struct HumanEngineCameraBalance {std::uint16_t angle=0;unsigned phase=0;};
HumanEngineCameraBalance human_engine_camera_balance(const std::array<float,3>& source,
    const std::array<float,3>& forward,const std::array<float,3>& eye,int offset,int interior,
    unsigned mover,unsigned pov)noexcept;


struct EngineAudioVoice {
    std::uint32_t sample = 0, state = 0;
    std::int32_t pitch_q12 = 4096, gain_q15 = 0, pan = 0;
};

// Load-time translation of the straight-line PLAYER_CAR ABK wiring into
// bounded data operations. No executable pages or x86 execution at runtime.
// Original integer lookup/product/edge/ramp callbacks; bank tables stay retail.
class EngineAudioGraph {
public:
    void load(const std::vector<std::uint8_t>& bank, unsigned group, unsigned upgrade);
    std::array<EngineAudioVoice,4> tick(float rpm, float load_percent,
        float milliseconds = 1000.0F/60.0F,
        unsigned gain_a = 32767, unsigned gain_b = 32767,
        std::uint16_t pan = 32768);
    std::size_t resident_bytes() const noexcept;
    // Shared checked ABK wiring for the selected race effects. Player feedback
    // is supplied by the portable PCM backend, including one-shot completion.
    void load_effect(const std::vector<std::uint8_t>& bank);
    void reset_effect();
    // Null uses the process SND library; a caller can supply a separate
    // explicitly initialized library context for replay or comparison.
    void set_random_context(BankAudioRandom* value)noexcept {random_context_=value;}
    void effect_tick(const std::vector<std::int32_t>& parameters,
        const std::function<int(unsigned,const EngineAudioVoice&)>& player,
        float milliseconds=1000.0F/60.0F);
    unsigned player_count() const noexcept {return players_;}
    bool retired() const noexcept {return retired_;}
private:
    struct Op { unsigned kind, at, value; };
    std::vector<std::uint8_t> data_;
    std::vector<Op> ops_;
    std::vector<std::uint8_t> initial_;
    unsigned params_ = 0, group_ = 0, upgrade_ = 0;
    unsigned parameter_count_=0,players_=0;
    bool effect_=false,retired_=false;
    // Bank callbacks share process SND state; callers serialize graph updates.
    BankAudioRandom* random_context_=nullptr;
    std::uint32_t random_word()noexcept;
    std::function<int(unsigned,const EngineAudioVoice&)> player_;
    void compile(const std::vector<std::uint8_t>& bank,bool effect);
    std::uint32_t read_word(unsigned at) const;
    std::int32_t integer(unsigned at) const;
    float real(unsigned at) const;
    void write_word(unsigned at, std::uint32_t value);
    void real(unsigned at, float value);
    std::int32_t callback(unsigned id, unsigned at, float dt,
        std::array<EngineAudioVoice,4>& voices, unsigned& count);
};

} // namespace nfsu::audio
