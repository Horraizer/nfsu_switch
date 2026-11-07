#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
namespace nfsu::audio {
// Original5368B0/536A70 profile admission and536450 gain publication.
// Owner0/categories1/9/10 and global music1 are rendered by this adapter.
class RaceAudioMix {
public:
    struct Profile {int priority;float attack,release;int engine,music;};
    static constexpr std::array<Profile,12> profiles{{
        {0,1.5F,1.5F,16358,0},
        {1,1.5F,1.5F,0,26500},
        {1,1.5F,1.5F,16358,1000},
        {1,1.5F,1.5F,16358,32767},
        {5,0.100000001F,0.300000012F,0,32767},
        {10,0.F,0.5F,16358,32767},
        {0,0.F,0.F,0,0},
        {50,0.F,0.200000003F,0,32767},
        {100,0.F,0.F,16358,26500},
        {101,0.25F,2.F,16358,6300},
        {11,5.F,0.5F,16358,32767},
        {2,3.F,3.F,16358,32767},
    }};
    static constexpr std::array<int,12> environment_targets{{0,10000,31500,32500,28000,29500,0,28000,0,0,31000,24000}};
    static constexpr std::array<int,12> human_targets{{0,13500,21000,18000,22500,16000,0,22000,0,0,14500,9500}};
    static constexpr std::array<int,12> shift_targets{{0,23000,32500,21500,18000,28500,0,8000,0,0,14000,15000}};
    static constexpr std::array<int,12> turbo_targets{{0,7000,15000,10000,12500,13000,0,5000,0,0,8000,7000}};
    static constexpr std::array<int,12> collision_targets{{0,32767,29500,26500,28500,20500,0,28500,0,0,28500,18500}};
    void request(unsigned profile) noexcept {
        if(profile>=profiles.size()||profile==current_)return;
        if(profile!=0 && profiles[profile].priority<profiles[current_].priority)return;
        select(profile,profiles[profile].attack);
    }
    void clear(unsigned profile) noexcept {
        if(profile>=profiles.size()||!active_[profile])return;
        active_[profile]=false;
        if(profile!=current_)return;
        unsigned next=0;
        for(unsigned i=0;i<active_.size();++i)
            if(active_[i]&&profiles[i].priority>profiles[next].priority)next=i;
        select(next,profiles[profile].release);
    }
    void tick(float dt) noexcept {
        if(!(dt>=0.F&&dt<=.1F))return;
        engine_.tick(dt);music_.tick(dt);environment_.tick(dt);human_.tick(dt);turbo_.tick(dt);shift_.tick(dt);collision_.tick(dt);
        published_={{engine_.value(),music_.value(),environment_.value(),human_.value(),turbo_.value(),shift_.value(),collision_.value()}};
    }
    unsigned profile()const noexcept{return current_;}
    int engine()const noexcept{return published_[0];}
    int environment()const noexcept{return published_[2];}
    int human_engine()const noexcept{return published_[3];}
    int turbo()const noexcept{return published_[4];}
    int shift()const noexcept{return published_[5];}
    int music()const noexcept{return published_[1];}
    int collision()const noexcept{return published_[6];}
    static std::uint32_t apply(std::uint32_t input,int gain)noexcept {
        return std::uint32_t(std::clamp((std::int64_t(input)*gain)>>15,INT64_C(0),INT64_C(32767)));
    }
    std::uint32_t music_output()const noexcept{return apply(32767,music());}
    // Fresh4AA200 preferences ->5225C0 ->52EA00; human source is its listener car.
    static constexpr std::uint32_t default_human_position_gain=19437;
    // Fresh4AA21E preferences, normal5365E0 impact output (FX option0.9).
    static constexpr int default_impact_global_gain=31689;
    // Music context ->539AC0 steady descriptor, unity selected song volume.
    static constexpr std::uint32_t default_music_descriptor_gain=26539;
    static int native_product(std::uint32_t a,std::uint32_t b)noexcept {
        const std::uint32_t product=a*b;
        return int(product>>15)-(product&0x80000000U?131072:0);
    }
    static std::uint32_t spatial_output(std::uint32_t input,int position,int category)noexcept {
        const int positional=native_product(input,std::uint32_t(position));
        return std::uint32_t(native_product(std::uint32_t(positional),std::uint32_t(category)));
    }
    static std::uint32_t engine_output(std::uint32_t input,int category)noexcept {
        return spatial_output(input,int(default_human_position_gain),category);
    }
    static std::uint32_t music_output(int profile_gain)noexcept {
        return apply(default_music_descriptor_gain,profile_gain);
    }
    std::uint32_t music_device_gain()const noexcept{return music_output(music());}
    void begin_frame(bool nitrous,float dt=1.F/60.F)noexcept {
        if(!racing_){request(3);racing_=true;}
        tick(dt);
        if(nitrous!=nitrous_){if(nitrous)request(5);else clear(5);nitrous_=nitrous;}
    }
    // Original52C1D0, after human controls and NOS requests. Gains remain the
    // preceding536800 publication until the next begin_frame.
    void human(float time,float speed,float throttle_on,float throttle_off,
               float last_shift,float audible_rpm,int gear,std::int32_t race_ticks,
               bool crash)noexcept {
        if(throttle_on>throttle_off && time>throttle_on+6.F && time>last_shift+6.F &&
           audible_rpm>6000.F && gear>=6 && double(speed)*2.237>150.)request(10);
        else clear(10);
        if(double(speed)*2.237<6. && !idle_){idle_=true;idle_since_=time;}
        if(double(speed)*2.237>6. && idle_)idle_=false;
        if(idle_ && time>idle_since_+3.F && double(race_ticks)*.00025>3.)request(11);
        else clear(11);
        if(crash)request(7);else clear(7);
    }
    void race(bool nitrous,bool crash,float dt=1.F/60.F)noexcept {
        begin_frame(nitrous,dt);
        if(crash)request(7);else clear(7);
    }
private:
    struct Curve {
        int target=16358;
        float start=0.F,elapsed=0.F,duration=0.F,offset=0.F;
        void set(int next,float seconds) noexcept {
            start=float(target-next);target=next;elapsed=0.F;duration=seconds>0.F?seconds:0.01F;
            offset=start;
        }
        void tick(float dt)noexcept {
            elapsed+=dt;
            offset=elapsed<=duration&&duration>0.F ? (elapsed/duration)*(-start)+start : 0.F;
        }
        int value()const noexcept{return std::min(32767,target+int(offset));}
    };
    void select(unsigned profile,float seconds)noexcept {
        current_=profile;active_[profile]=true;
        engine_.set(profiles[profile].engine,seconds);
        music_.set(profiles[profile].music,seconds);
        environment_.set(environment_targets[profile],seconds);
        human_.set(human_targets[profile],seconds);
        turbo_.set(turbo_targets[profile],seconds);
        shift_.set(shift_targets[profile],seconds);
        collision_.set(collision_targets[profile],seconds);
    }
    std::array<int,7> published_{{16358,16358,16358,16358,16358,16358,16358}};
    Curve engine_,music_,environment_,human_,turbo_,shift_,collision_;
    bool racing_=false,nitrous_=false,idle_=false;
    float idle_since_=0;
    unsigned current_=0;
    std::array<bool,12> active_{};
};
}
