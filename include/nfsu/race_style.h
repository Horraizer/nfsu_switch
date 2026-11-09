#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
namespace nfsu {
constexpr unsigned kRaceStyleKinds=31;
struct RaceStyleRule {
    std::uint32_t language_hash=0;
    bool continuous=false,enabled=false;
    float duration=0,timeout=0,minimum=0,limit=0,rate=0;
    int multiplier_limit=1,multiplier_step=0,count_limit=-1;
    std::string label;
};
struct RaceStyleItem {
    int phase=2,multiplier=1,count=0;
    float points=0,raw=0,total=0,factor=1,longest=0,best=0;
    std::int32_t touched=0,began=0;
};
struct RaceStyleNotice {
    unsigned kind=0;
    float points=0;
    int multiplier=1;
    std::int32_t shown=0;
    bool continuous=false,failed=false,started=false;
};
// Separate drift item0 record of4A85E0/4A6EB0. Failed publication keeps
// the previously displayed points; general notices use a different queue.
struct DriftScorePublication {
    float points=0;int multiplier=1;
    std::uint64_t sequence=0;
    std::int32_t published=0;
    bool failed=false;
    void publish(float value,int mult,bool loss,std::int32_t now) noexcept {
        if(!loss)points=value;
        multiplier=mult;failed=loss;published=now;
        if(++sequence==0)++sequence;
    }
};
// Original 436230/436070/436940 state machine. No file access, allocation,
// physics changes or wall clock after load. Drift-mode producers are separate.
class RaceStyle {
public:
    bool load(const std::filesystem::path& root,std::string& error,bool drift=false);
    void configure(const std::array<RaceStyleRule,kRaceStyleKinds>& rules,bool drift=false);
    bool ready() const noexcept {return ready_;}
    void trigger(unsigned kind,float factor,std::int32_t now) noexcept;
    void break_chain(unsigned kind) noexcept;
    void cancel_active() noexcept;
    void cancel(unsigned kind) noexcept;
    void set_multiplier(unsigned kind,int value) noexcept;
    void finish(std::int32_t now) noexcept;
    void update(std::int32_t now,float seconds) noexcept;
    int total() const noexcept;
    const RaceStyleNotice* notice() const noexcept;
    void consume_drift_notice() noexcept;
    const DriftScorePublication& drift_publication() const noexcept {return drift_publication_;}
    const RaceStyleRule& rule(unsigned i) const {return rules_[i];}
    const RaceStyleItem& item(unsigned i) const {return items_[i];}
private:
    std::array<RaceStyleRule,kRaceStyleKinds> rules_{};
    std::array<RaceStyleItem,kRaceStyleKinds> items_{};
    std::array<RaceStyleNotice,kRaceStyleKinds> notices_{};
    std::optional<RaceStyleNotice> retired_notice_;
    DriftScorePublication drift_publication_;
    unsigned count_=0;
    int total_=0;
    bool ready_=false;
    bool drift_=false;
    double accrue(unsigned,float) noexcept;
    void publish(unsigned,std::int32_t) noexcept;
    void erase_notice(unsigned) noexcept;
};
// Inputs are published once per world frame, after timing. Contact admission
// and ground queries belong to the session, not the point state machine.
struct RaceStyleDrivingSample {
    std::int32_t now=0;
    float seconds=0,race_seconds=0,speed=0,forward_speed=0,slip_degrees=0;
    float up_z=1,height=0,yaw_rate=0,heading_degrees=0,drafting=0;
    unsigned support=4,position=1,racers=1;
    bool racing=false,finished=false,new_checkpoint=false,new_lap=false;
    bool impact=false,ground_valid=false,shortcut=false;
};
// 426300/426450: leading actor velocity cone, speed-dependent reach.
float race_drafting_pair(const std::array<float,3>& leader_position,
    const std::array<float,3>& leader_velocity,float leader_speed,
    const std::array<float,3>& follower_position) noexcept;
struct RaceNearPass {
    std::uint64_t identity=0;
    float hull_distance=0,relative_speed=0;
    bool contact=false;
};
// 43F630: acquire one close, fast-relative pair; award on separation only.
class RaceStyleNearMiss {
public:
    void impact(std::int32_t now) noexcept {impact_at_=now;have_impact_=true;candidate_=0;}
    bool tracks(std::uint64_t identity) const noexcept {return candidate_==identity;}
    void update(RaceStyle&,std::int32_t now,float speed,const RaceNearPass*,std::size_t count) noexcept;
private:
    std::uint64_t candidate_=0;
    std::int32_t impact_at_=0;
    bool have_impact_=false;
};
class RaceStyleDriving {
public:
    void update(RaceStyle&,const RaceStyleDrivingSample&) noexcept;
private:
    bool section_=false,lap_=false,clean_=true,checkpoint_edge_=false,lap_edge_=false,finished_=false;
    std::int32_t impact_at_=0;
    unsigned spin_=0;
    float spin_heading_=0;
};
}
