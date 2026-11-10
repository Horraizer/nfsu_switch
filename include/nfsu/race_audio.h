#pragma once
#include "nfsu/backfire_audio.h"
#include "nfsu/frontend_sfx.h"

#include "nfsu/audio.h"
#include "nfsu/audio_playback.h"
#include "nfsu/collision_audio.h"
#include "nfsu/engine_audio.h"
#include "nfsu/turbo_audio.h"
#include "nfsu/race_session.h"
#include "nfsu/traffic_audio.h"

#include <cstdint>
#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nfsu::audio {

static_assert(kDriveStepsPerFrame *
    std::tuple_size<decltype(VehicleTrackTelemetry::audio_contacts)>::value <=
    kCollisionContactFrameLimit, "scheduler must accept every bounded race contact");

// Copied only after a race tick. It has no pointer back into RaceSession.
struct RaceAudioSnapshot {
    double rpm = 0;
    std::array<float,3> human_position{},human_forward{{1,0,0}};
    unsigned camera_mover=1,camera_pov=0;
    double maximum_rpm = 10000; //Synthetic bank-unit snapshots default to an identity scale.
    double throttle = 0;
    double engine_drive_load = 0,brake = 0;
    float gear_ratio=1,first_gear_ratio=1;
    float torque_maximum=160.F; // Synthetic snapshots; live526420 uses selected RideInfo curve.
    bool handbrake = false;
    int engine_game_state = 4;
    double speed_mps = 0;
    float owner_speed=0;
    std::int32_t race_ticks=0;
    double wheel_slip = 0;
    std::array<double,2> axle_long_slip{},axle_lateral_slip{};
    double yaw_slip_radians = 0;
    int gear = 0;
    std::uint64_t tick = 0;
    // Latest solver slice, independent of countdown/race tick epoch.
    std::uint64_t physics_step = 0;
    std::array<CollisionContact, kCollisionContactFrameLimit> contacts{};
    std::size_t contact_count = 0;
    VehiclePropContactFrame prop_contacts;
    bool collision_reset = false;
    int countdown = 0;
    int completed_laps = 0;
    bool finished = false;
    bool nitrous_active = false;
    bool turbo_installed=false;
    float turbo_normalized=0;
    bool crash_cinematic = false;
    struct Rival {
        EngineEnvelopeInput engine;
        std::array<float,3> position{},forward{{1,0,0}};
        bool active=false,nitrous=false,turbo_installed=false;
    };
    std::array<Rival,3> rivals{};
    float countdown_seconds=0;
    struct Traffic {TrafficAudioInput input;std::uint64_t generation=0;bool active=false;};
    std::array<Traffic,kRaceTrafficCapacity> traffic{};
    std::array<float,3> listener_eye{},listener_forward{{1,0,0}};

};

struct HumanTurboFeedback {float rpm=0,load=0;int pan=0,balance=0;bool throttle_on=false;};

struct RaceShiftSoundCue {unsigned kind=0;std::int32_t gain=0;unsigned disengage=0;std::int32_t disengage_gain=0;std::uint16_t pan=32768;unsigned bank_variant=0;};
// Separate engine/exhaust graphs and decoded samples for the selected car.
// Engine occupies voices0..3; exhaust12..15. Other race cues retain their slots.
class RaceEngineSound {
public:
    bool load(const std::filesystem::path& root,const std::string& car,int upgrade,
              std::string& error);
    RaceShiftSoundCue update(const RaceAudioSnapshot& snapshot,Mixer& mixer,
                std::uint32_t category_gain = 16358,int shift_category_gain = 16358);
    bool load_remote(const std::filesystem::path&,const std::string&,int,unsigned,std::string&);
    RaceShiftSoundCue update_remote(const RaceAudioSnapshot::Rival&,const RaceAudioSnapshot&,Mixer&,int,
                       TrafficAudioRandom&,TrafficAudioRandom&,int shift_category=16358,int end_category=16358);
    void stop(Mixer& mixer) noexcept;
    std::size_t resident_bytes() const noexcept;
    const EngineAudioSelection& selection() const noexcept {return selection_;}
    HumanEngineProfileState profile_state()const noexcept {return envelope_.profile_state();}
    HumanTurboFeedback turbo_feedback()const noexcept {return human_turbo_;}
private:
    EngineAudioSelection selection_;
    HumanTurboFeedback human_turbo_;
    HumanEngineEnvelope envelope_;
    EngineEnvelope remote_envelope_;
    RemoteRevScript remote_script_;
    RemoteAudioPosition remote_position_;
    unsigned remote_slot_=0;
    bool remote_=false;
    unsigned slot(unsigned index)const noexcept {return remote_?remote_slot_+index:index<4?index:index+8;}
    void publish(float,float,const std::array<unsigned,2>&,float,std::uint16_t,Mixer&);
    std::array<EngineAudioGraph,2> graphs_;
    std::array<std::array<PcmClip,4>,2> clips_;
    std::array<std::uint32_t,8> state_{},sample_{};
    int gain_a_=0,gain_b_=0,balance_=0,interior_balance_=0;
    std::uint64_t tick_=0;
    bool loaded_=false,have_tick_=false;
};

// Authored retail effect graphs with bounded voices and mixer completion
// feedback. The physical-to-skid input adapter is explicitly port-side.
class RaceEffectsSound {
    friend class RaceTurboSound;
    friend class RaceRemoteShiftSound;
public:
    bool load(const std::filesystem::path& root,const std::string& car,int engine,
              int nitrous,int tyres,std::string& error);
    void update(const RaceAudioSnapshot& snapshot,Mixer& mixer,const RaceShiftSoundCue& shift = {},
                const HumanTurboFeedback* human=nullptr,int backfire_category=16358,TrafficAudioRandom* random=nullptr);
    void stop(Mixer& mixer) noexcept;
    // Bind an explicitly owned SND-library RNG after loading; null is shared.
    void set_random_context(BankAudioRandom*)noexcept;
    std::size_t resident_bytes() const noexcept;
private:
    struct Effect {
        EngineAudioGraph graph;
        std::array<PcmClip,11> clips;
        std::array<unsigned,4> slots{},states{},samples{};
        std::vector<std::int32_t> parameters;
        unsigned gain_divisor=2; // Remaining NOS/skid output adapter; shift uses original1.
        void run(Mixer& mixer,const std::array<PcmClip,11>* shared=nullptr);
        void reset(Mixer& mixer);
        void stop(Mixer& mixer) noexcept;
    };
    Effect nitrous_,shift_,backfire_;
    BackfireSoundController backfire_control_;
    unsigned engine_level_=0;bool backfire_started_=false;
    std::array<Effect,2> disengage_;
    std::array<bool,2> disengage_started_{};
    std::array<Effect,2> skid_;
    std::uint64_t tick_=0;
    bool loaded_=false,have_tick_=false,nitrous_active_=false,nitrous_started_=false,shift_started_=false;
};

// Original remote523990/523E30 shift actors. Two fixed voices per rival;
// the later end actor shares the selected main actor PCM, not human controls.
class RaceRemoteShiftSound {
public:
    bool load(const std::filesystem::path&,int,unsigned,std::string&);
    void update(const RaceAudioSnapshot&,bool,Mixer&,const RaceShiftSoundCue&);
    void stop(Mixer&)noexcept;
    std::size_t resident_bytes()const noexcept;
private:
    RaceEffectsSound::Effect shift_,end_;
    std::uint64_t tick_=0;
    bool loaded_=false,have_tick_=false,shift_started_=false,end_started_=false;
};
static_assert(98+2*3<=kMixVoices,"rival shift range exceeds mixer");

// Selected Turbo actor; two fixed voices share decoded samples.
// Human85/86 and remote104..109 ranges are disjoint.
static_assert(104+2*3<=kMixVoices,"rival Turbo range exceeds mixer");
class RaceTurboSound {
public:
    bool load(const std::filesystem::path&,const std::string&,int,std::string&);
    void update(const RaceAudioSnapshot&,const HumanTurboFeedback&,Mixer&,int,TrafficAudioRandom&);
    bool load_remote(const std::filesystem::path&,const std::string&,int,unsigned,std::string&);
    void update_remote(const RaceAudioSnapshot&,const RaceAudioSnapshot::Rival&,
                       const HumanTurboFeedback&,Mixer&,int,TrafficAudioRandom&);
    void stop(Mixer&)noexcept;
    std::size_t resident_bytes()const noexcept;
private:
    RaceEffectsSound::Effect spool_,release_;
    TurboSoundTuning tuning_;
    TurboSoundController control_;
    TurboAudioPosition position_;
    bool remote_=false;
    void publish(const TurboSoundOutput&,Mixer&);
    std::uint64_t tick_=0;
    bool loaded_=false,have_tick_=false,release_started_=false;
};

// Authenticated first IMPACT module (IMPACT_SMOKABLE), not world samples.
// Two voices and last-touch episodes are bounded port output policy.
class RacePropSound {
public:
    bool load(const std::filesystem::path& root,std::string& error,std::uint32_t materials=0xffffffffU);
    void update(const RaceAudioSnapshot&,Mixer&,int category_gain=32768,int global_gain=32768);
    // Separate admission from bank callbacks so car/road/prop starts share order.
    bool prepare(const RaceAudioSnapshot&,Mixer&);
    void render_voice(unsigned,Mixer&,int category_gain,int global_gain);
    std::uint32_t voice_order(unsigned i) const noexcept {
        return i<voices_.size()&&voices_[i].live?voices_[i].order:UINT32_MAX;
    }
    void stop(Mixer&) noexcept;
    std::size_t resident_bytes() const noexcept;
private:
    struct Voice {EngineAudioGraph graph;bool live=false,rendered=false;
        unsigned state=0,sample=0,variant=0;int intensity=0;
        std::uint32_t order=0,pair=UINT32_MAX;};
    struct Pair {std::uint32_t id=UINT32_MAX;std::uint64_t step=0;float magnitude=0;};
    std::array<PcmClip,30> clips_;
    std::array<unsigned,29> variants_{};
    std::array<Voice,2> voices_;
    std::array<Pair,32> pairs_;
    std::uint64_t tick_=0;
    unsigned next_=0;
    bool loaded_=false,have_tick_=false;
};

// Start accepted world/car impacts and tick prop graphs in shared solver order.
// Queue admission and the bounded backend voice counts remain separate policy.
void dispatch_race_impacts(RacePropSound*,Mixer&,CollisionVoice* const*,std::size_t,
    int category_gain,int global_gain,const std::function<void(CollisionVoice&)>&);

// Original ENV_STATIC traffic event controller and bank, shared PCM per pool.
// Fixed two voices per traffic actor; no loads or allocation in update().
constexpr unsigned race_traffic_voice_slot(unsigned actor,unsigned side) noexcept {
    // Keep the original port ranges29..60 and61..86 disjoint. Extra traffic
    // gets new slots after the existing remote-engine and turbo actors.
    return (actor<16?29+actor*2:87+(actor-16)*2)+side;
}
static_assert(race_traffic_voice_slot(kRaceTrafficCapacity-1,1)<kMixVoices,"traffic voice range exceeds mixer");
class RaceTrafficSound {
public:
    bool load(const std::filesystem::path&,std::string&,unsigned count=kRaceTrafficCapacity);
    void update(const RaceAudioSnapshot&,Mixer&,int environment_gain,TrafficAudioRandom* shared_random=nullptr);
    void stop(Mixer&) noexcept;
    std::size_t resident_bytes()const noexcept;
private:
    struct Voice {EngineAudioGraph graph;std::vector<std::int32_t> parameters;
        unsigned state=0,sample=0;};
    struct Actor {TrafficAudioController controller;TrafficAudioPosition position;
        std::array<Voice,2> voices;std::uint64_t generation=0;bool active=false;};
    std::vector<Actor> actors_;
    std::array<PcmClip,14> clips_;
    TrafficAudioRandom random_;
    std::uint64_t tick_=0;bool loaded_=false,have_tick_=false;
};

RaceAudioSnapshot race_audio_snapshot(const RaceSession& session) noexcept;
// Previous port mix, retained only for diagnostic before/after playback.
std::array<double, 4> engine_layer_weights(double rpm) noexcept;
std::vector<Segment> race_music_playlist(const Bank& bank,
                                         const std::vector<Segment>& indexed);

enum class RaceInterfaceCue : std::uint8_t {
    none, countdown, go, lap, finish
};

// Consumes only copied race state. Finish takes precedence over a final-lap
// crossing on the same tick.
class RaceCueScheduler {
public:
    RaceInterfaceCue update(const RaceAudioSnapshot& snapshot) noexcept;
private:
    int countdown_ = 0;
    int completed_laps_ = 0;
    bool finished_ = false;
};

// Reads retail audio and drives the platform output. Missing banks or a
// missing device leave the race running with an explicit diagnostic.
struct RaceRemoteEngineBinding {std::string car;int engine_upgrade=0,induction_upgrade=0;bool turbo_installed=false;};
class RaceAudioService {
public:
    struct Stats {
        std::uint64_t ring_underruns = 0;
        std::size_t ring_fill = 0;
        std::size_t ring_capacity = 0;
        std::uint64_t output_late = 0;
        std::uint32_t queued_frames = 0;
        std::uint32_t thread_us = 0;
        std::uint64_t mix_clips = 0;
        std::uint64_t mix_overloads = 0;
        std::uint64_t mix_limited_frames = 0;
    };
    RaceAudioService();
    ~RaceAudioService();
    RaceAudioService(const RaceAudioService&) = delete;
    RaceAudioService& operator=(const RaceAudioService&) = delete;
    bool start(const std::filesystem::path& retail_root, std::string& diagnostic,
               const std::function<void(const char*)>& progress = {},
               const std::function<bool()>& decoder_thread_setup = {},
               bool original_ig_playlist = false,
               const std::string& car = "240SX", int engine_upgrade = 0,
               int nitrous_upgrade = 1,int tyre_upgrade = 0,
               const std::vector<RaceRemoteEngineBinding>& rivals = {},int induction_upgrade = 0,
               unsigned traffic_count = 16,std::uint32_t prop_materials = 1);
    void on_tick(const RaceAudioSnapshot& snapshot,std::uint32_t* world_random=nullptr);
    void set_suspended(bool suspended) noexcept;
    void fade_music_for_finish() noexcept;
    void frontend_sound(FrontendSound);
    bool frontend_sound_active();
    bool post_race_voice(const std::filesystem::path&,unsigned event,bool won,std::string& error);
    void set_ui_mix_percent(unsigned music,unsigned effects)noexcept;
    void stop();
    std::string diagnostic() const;
    Stats stats() const noexcept;
    std::size_t resident_bytes() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nfsu::audio
