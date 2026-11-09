#include "nfsu/race_drift.h"
#include "nfsu/race_events.h"
#include "nfsu/track_zones.h"
#include <cstring>
#include <fstream>
#include <algorithm>
#include <cmath>
#include "nfsu/ai_powertrain.h"
namespace nfsu {
bool DriftCompetition::load(const std::filesystem::path& root,unsigned number,unsigned difficulty,
                            unsigned grade,float performance,std::uint32_t seed,std::string& error,
                            const std::string& player_name) {
    if(!is_supported_event(number) || difficulty>2 || grade>3 || !std::isfinite(performance)) {
        error="unsupported drift competition";return false;
    }
    std::vector<unsigned char> bytes;
    const auto events=load_race_event_table(root/"Frontend/FRONTB.LZC",bytes);
    const auto* event=find_race_event(events,number);
    if(!events.ok() || !event || event->type!=3 || event->offset>bytes.size() ||
       kRaceEventRecordSize>bytes.size()-event->offset) {
        error="drift event descriptor invalid";return false;
    }
    const auto scalar=[](const unsigned char* p){float f;std::memcpy(&f,p,4);return f;};
    const auto* raw=bytes.data()+event->offset;
    const float event_scale=scalar(raw+0x13C+4*difficulty);
    if(!std::isfinite(event_scale)||event_scale<0){error="invalid drift difficulty scalar";return false;}
    DriftCompetition next;next.grade_=grade;next.standings_[0]={player_name,0,0};
    std::array<unsigned,3> contexts{};
    for(unsigned i=0;i<3;++i) {
        const auto* p=raw+0xD4+i*0x1C;
        const auto* end=static_cast<const unsigned char*>(std::memchr(p,0,12));
        if(!end||end==p){error="invalid drift rival name";return false;}
        for(auto c=p;c!=end;++c)if(*c<32||*c>126){error="invalid drift rival text";return false;}
        const unsigned context=p[0x10+2*difficulty]|unsigned(p[0x11+2*difficulty])<<8;
        if(context>=10){error="drift rival requires unsupported random context selection";return false;}
        contexts[i]=context;next.standings_[i+1]={std::string(p,end),0,i+1};
    }
    std::ifstream file(root/"Global/GLOBALB.BUN",std::ios::binary|std::ios::ate);
    if(!file||file.tellg()<0||file.tellg()>64*1024*1024){error="drift configuration read refused";return false;}
    bytes.resize(std::size_t(file.tellg()));file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))) {
        error="drift configuration read failed";return false;
    }
    RaceChunkPayload payload;std::vector<RaceTableIssue> issues;
    if(!find_race_chunk(bytes.data(),bytes.size(),0x34B00,payload,issues)||payload.end-payload.begin!=0x82C) {
        error="drift AI configuration invalid";return false;
    }
    // 43E04C..43E0CD: normalized acceleration rating, clamped to [0,10],
    // interpolated between .65 and 1 then clamped. Global gameplay scales
    // 6B78FC/6B7900 are both 1 in the supported retail configuration.
    const float rating=std::clamp(float((double(performance)-140)*10/(771-140)),0.F,10.F);
    const float scale=std::clamp(float(double(rating)*(1-double(.6499999761581421F))+
                                     .6499999761581421F),.6499999761581421F,1.F);
    // 43DD40 redraws all standings seeds via564A80, including the human. The port owns
    // its seed; it does not claim the PC front end consumed the same RNG calls.
    for(unsigned actor=0;actor<4;++actor) {
        const auto sample=seed%0x7FFFFFFFU,x=seed^0x1D872B41U,y=x^(x>>5);
        seed=(y<<27)^y^x;
        const float random=float(double(sample)*4.656612873077393e-10);
        if(!actor)continue;
        // Loader462070 copies the payload to779C78. Context base77A004
        // is +38C, so77A03C/40 are payload offsets3C4/3C8.
        const auto* row=bytes.data()+payload.begin+0x380+contexts[actor-1]*0x54;
        const float low=scalar(row+0x44),high=scalar(row+0x48);
        if(!std::isfinite(low)||!std::isfinite(high)||low<0||high<low) {
            error="invalid drift opponent score range";return false;
        }
        const float factor=((high-low)*random)+low;
        next.factors_[actor-1]=factor;
    }
    next.event_scale_=event_scale;next.performance_scale_=scale;
    next.ready_=true;*this=std::move(next);error.clear();return true;
}
void DriftCompetition::update(const RaceRouteTrack& track,bool reverse,const float point[2],
                              int highest_lap,float player_score) {
    if(!ready_)return;
    for(auto& row:standings_)if(row.actor==0)row.score=player_score;
    if(zone_visits_.size()!=track.zones.size())zone_visits_.assign(track.zones.size(),0);
    for(std::size_t i=0;i<track.zones.size();++i) {
        const auto& zone=track.zones[i];
        if(zone.type!=6 || (zone.direction_filter!=-1&&zone.direction_filter!=int(reverse)) ||
           point[0]<zone.box_min[0]||point[0]>zone.box_max[0]||point[1]<zone.box_min[1]||
           point[1]>zone.box_max[1] ||
           !point_in_track_zone_polygon(point,zone.vertices.data(),std::size_t(zone.vertex_count)))continue;
        if(zone_visits_[i]<=highest_lap) {
            ++zone_visits_[i];
            const float award=float(highest_lap==0?zone.drift_first[grade_]:zone.drift_later[grade_]);
            for(auto& row:standings_)if(row.actor) {
                const float increment=(award*factors_[row.actor-1])*event_scale_;
                row.score=increment*performance_scale_+row.score;
            }
        }
        break;
    }
    for(unsigned i=1;i<4;++i)for(unsigned j=i;j && standings_[j].score>standings_[j-1].score;--j)
        std::swap(standings_[j],standings_[j-1]);
}
unsigned DriftCompetition::position() const noexcept {
    for(unsigned i=0;i<4;++i)if(!standings_[i].actor)return i+1;
    return 1;
}
bool drift_reference_speed(const VehicleHandling& h,float& speed) noexcept {
    std::array<float,64> values{};if(!ai_setup_acceleration_curve(h,values))return false;
    constexpr float maximum=98.34600067138672F,step=1.3410818576812744F;
    float x=0;
    for(;x<maximum;x+=step) {
        const float index=x*(63.F/maximum);
        const unsigned i=unsigned(index);
        const float f=index-float(i);
        const float engine=i>=63?values[63]:(1-f)*values[i]+f*values[i+1];
        const float drag=((x*h.drag)*x)/h.mass;
        if(engine<drag)break;
    }
    speed=x;return std::isfinite(x);
}
void drift_combo_update(DriftCombo& c,float velocity,bool blocked,unsigned grade,
                        unsigned phase,float points,std::int32_t now) noexcept {
    if(grade>3||c.multiplier<1||c.multiplier>5)return;
    // 43E790. Reduction is immediate on entry, then at 1.5-second intervals.
    constexpr float speed[]={0,8.333333969116211F,11.111111640930176F,13.88888931274414F,16.666667938232422F};
    constexpr int targets[4][4]={{350,1400,4200,11200},{300,1200,3600,9600},
                               {250,1000,3000,8000},{200,800,2400,6400}};
    constexpr float margin=1.388888955116272F;
    c.warning=c.recovering=false;
    const double since=double(std::int64_t(now)-c.reduced_at)*.00025F;
    int next=c.multiplier;
    if(c.multiplier>1 && (blocked || velocity<double(speed[c.multiplier-1])+margin)) {
        c.warning=true;
        if(!c.timer||since>1.5F) {--next;c.reduced_at=now;c.timer=true;}
    } else if(c.timer) {
        c.recovering=true;
        if(since>1.5F){c.timer=false;c.reduced_at=0;}
    } else if(c.multiplier<4 && velocity<double(speed[c.multiplier])-margin) {
        c.recovering=true;
    } else if(phase==0 && c.multiplier<5 && points>c.target)++next;
    c.target=next<5?targets[grade][next-1]:0;
    c.multiplier=next;
}
void RaceDrift::update(RaceStyle& score,const DriftDrivingSample& s) noexcept {
    reset_requested_=false;reset_countdown_=-1;
    //43EDE0: wrong direction or negative forward speed; one second grace,
    // then three seconds countdown. This uses simulation time, not frames.
    if(s.motion.blocked && !finished_) {
        if(!wrong_way_timer_){wrong_way_timer_=true;wrong_way_since_=s.now;}
        const float elapsed=float(double(std::int64_t(s.now)-wrong_way_since_)*.00025F);
        if(elapsed>4){wrong_way_timer_=false;reset_requested_=true;}
        else if(elapsed>1)reset_countdown_=4-elapsed;
    }else wrong_way_timer_=false;
    if(!score.ready()||finished_||s.motion.grade>3||!(s.seconds>=0))return;
    const auto& item=score.item(0);angle_=s.motion.slip;
    // 43EF30 observes the preceding style state/zone before current admission.
    if(item.phase==0) {
        if(zone_>1)score.trigger(1,float(zone_)/2.F,s.now);
        if(achievement_<7 && item.points>score.rule(achievement_+4).duration) {
            score.trigger(achievement_+4,1,s.now);comment_.show(achievement_+4,s.now);++achievement_;
        }
        if(extreme_==0 || (double(angle_)*extreme_>0 && std::abs(angle_)>std::abs(extreme_)))extreme_=angle_;
    } else {achievement_=0;extreme_=0;score.break_chain(3);}
    auto motion=s.motion;motion.phase=unsigned(item.phase);
    const float factor=drift_score_factor(motion,highwater_);
    drift_combo_update(combo_,motion.speed,motion.blocked,motion.grade,
                       unsigned(item.phase),item.points,s.now);
    int next=combo_.multiplier;
    zone_=s.bonus_wheels>1 && s.motion.speed>double(s.reference_speed)*.25F?s.bonus_wheels:1;
    // 43E9D0 reads the most recently admitted collision for half a second.
    // A wall latches after the first read; subsequent contacts extend the
    // quiet interval without charging a penalty every display frame.
    if(s.impact_strength>0) {
        impact_strength_=s.impact_strength;impact_at_=s.now;
        impact_wall_=s.wall_impact;impact_valid_=true;
    }
    float impact=0;
    if(impact_valid_ && double(std::int64_t(s.now)-impact_at_)*.00025F<.5F) {
        if(!impact_latched_) {impact=impact_strength_;impact_latched_=impact_wall_;}
    } else impact_latched_=false;
    if(impact>.20000000298023224F) {
        next=1;score.cancel(0);
        if(impact_latched_&&impact>5)score.trigger(2,impact,s.now);
    } else if(factor>0) {
        score.set_multiplier(0,next);score.trigger(0,float(double(factor)*zone_),s.now);
        if(item.phase==0 && double(angle_)*extreme_<0 && std::abs(angle_)>double(.4000000059604645F)/1.5F) {
            if(std::abs(extreme_)>.4000000059604645F && motion.speed>17.88109016418457F)score.trigger(3,1,s.now);
            extreme_=angle_;
        }
    }
    combo_.multiplier=next;
    if(s.finished) {score.finish(s.now);finished_=true;}else score.update(s.now,s.seconds);
}
}
