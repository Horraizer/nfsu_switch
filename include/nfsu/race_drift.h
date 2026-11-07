#pragma once
#include "nfsu/drift.h"
#include "nfsu/race_style.h"
#include "nfsu/race_route.h"
namespace nfsu {
struct VehicleHandling;
struct DriftStanding { std::string name; float score=0; unsigned actor=0; };
// Virtual opponents: authored zone awards, no physical rival vehicles.
class DriftCompetition {
public:
    bool load(const std::filesystem::path&,unsigned event,unsigned difficulty,
              unsigned grade,float performance,std::uint32_t seed,std::string& error);
    void update(const RaceRouteTrack&,bool reverse,const float position[2],int highest_lap,float player_score);
    unsigned position() const noexcept;
    const std::array<DriftStanding,4>& standings() const {return standings_;}
private:
    std::array<DriftStanding,4> standings_{};
    std::array<float,3> factors_{};
    std::vector<int> zone_visits_;
    unsigned grade_=0;
    float event_scale_=1,performance_scale_=1;
    bool ready_=false;
};
bool drift_reference_speed(const VehicleHandling&,float&) noexcept;
struct DriftCombo {
    int multiplier=1,target=0;
    std::int32_t reduced_at=0;
    bool timer=false,warning=false,recovering=false;
};
void drift_combo_update(DriftCombo&,float speed,bool blocked,unsigned grade,
                        unsigned phase,float points,std::int32_t now) noexcept;
struct DriftDrivingSample {
    DriftScoreSample motion;
    std::int32_t now=0;
    float seconds=0,reference_speed=0,impact_strength=0;
    unsigned bonus_wheels=0;
    bool wall_impact=false,finished=false;
};
//43EF30 ->4A7920/4A7AA0: independent localized achievement message.
struct DriftComment {
    unsigned kind=0;std::int32_t shown=0;
    void show(unsigned value,std::int32_t now)noexcept {kind=value;shown=now;}
    unsigned visible(std::int32_t now)const noexcept {
        return kind&&float(std::int64_t(now)-shown)*.00025F<=2.F?kind:0;
    }
};
// Original producer 43E310, with explicitly supplied route/surface/collision
// operands. The adapter must supply those in their original units/epoch.
class RaceDrift {
public:
    void update(RaceStyle&,const DriftDrivingSample&) noexcept;
    int multiplier() const {return combo_.multiplier;}
    int target() const {return combo_.target;}
    bool warning() const {return combo_.warning;}
    bool recovering() const {return combo_.recovering;}
    unsigned zone() const {return zone_;}
    float angle() const {return angle_;}
    bool finished() const {return finished_;}
    bool reset_requested() const {return reset_requested_;}
    float reset_countdown() const {return reset_countdown_;}
    unsigned comment(std::int32_t now)const noexcept {return comment_.visible(now);}
    std::int32_t comment_started()const noexcept {return comment_.shown;}
private:
    float highwater_=0,extreme_=0,angle_=0,impact_strength_=0;
    std::int32_t impact_at_=0;
    bool impact_valid_=false,impact_wall_=false,impact_latched_=false;
    DriftCombo combo_;
    DriftComment comment_;
    unsigned achievement_=0,zone_=0;
    bool finished_=false,wrong_way_timer_=false,reset_requested_=false;
    std::int32_t wrong_way_since_=0;
    float reset_countdown_=-1;
};
}
