#pragma once
#include "nfsu/frontend.h"
#include "nfsu/race_session.h"
#include "nfsu/race_lap_hud.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace nfsu {
// Original586580 flags0/2 suppress zero minutes; flags2 reserves the
// first column for '+'. English formatting is the existing native5 adapter.
inline std::string post_race_time(std::int32_t ticks,bool gap=false) {
    RaceLapHudText text;
    if(!format_lap_hud_ticks(ticks,RaceLapHudStyle::native5,text))return "--:--.--";
    if(ticks>=0 && ticks<240000)std::snprintf(text.data(),text.size(),"%5d.%02d",ticks/4000,(ticks%4000)/40);
    if(gap && ticks>=0) {
        if(text[0]!=' ') {
            const auto length=std::strlen(text.data());
            std::memmove(text.data()+1,text.data(),length-1);
        }
        text[0]='+';
    }
    return text.data();
}
// PostRaceStats49CEE0 circuit binding. Speed/distance are measured from this
// port's completed fixed physics steps, not from render or wall-clock cadence.
// This is a statistics adapter, not a claim of retail telemetry bit parity.
class RaceResultStatistics {
public:
    void observe(const RaceSession& session) {
        const auto& t=session.timing();
        if(t.step<last_step_)*this={};
        if(t.step==last_step_)return;
        last_step_=t.step;
        if(!t.go_valid || t.step<=t.go_step || t.racers.empty())return;
        for(unsigned actor=0;actor<t.racers.size()&&actor<samples_.size();++actor) {
            auto& sample=samples_[actor];if(sample.done)continue;
            const auto& driver=actor?session.rival(actor-1):session;
            const auto& v=driver.car().velocity;
            const double speed=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
            const double dt=session.world_frame_seconds();
            sample.top=std::max(sample.top,speed);sample.distance+=speed*dt;sample.seconds+=dt;
            const auto& racer=t.racers[actor];
            for(unsigned lap=1;lap<sample.positions.size();++lap) {
                if(sample.positions[lap]||racer.lap_ticks[lap]<=0)continue;
                if(session.is_drift()) {
                    sample.positions[lap]=session.player_position();
                    const float total=session.style().item(0).total;
                    sample.scores[lap]=total-sample.last_score;sample.last_score=total;continue;
                }
                unsigned position=1;
                for(std::size_t n=0;n<t.racers.size();++n)if(n!=actor) {
                    const auto other=t.racers[n].lap_total_ticks[lap];
                    if(other>0&&other<racer.lap_total_ticks[lap])++position;
                }
                sample.positions[lap]=position;
            }
            sample.done=racer.finish_valid||racer.status!=RacerStatus::not_finished;
        }
    }
    double player_metres()const noexcept{return samples_[0].distance;}
    PostRaceValues values(const RaceSession& session,const std::string& car,unsigned actor=0)const {
        const auto& t=session.timing();const auto& r=t.racers.at(actor);
        const auto& sample=samples_.at(actor);
        const auto& driver=actor?session.rival(actor-1):session;
        const std::string name=actor?session.rival_name(actor-1):session.player_name();
        const auto& positions_=sample.positions;const auto& scores_=sample.scores;
        const auto top_=sample.top,distance_=sample.distance,seconds_=sample.seconds;
        PostRaceValues out;
        if(session.is_drift()) {
            //49CD90/49D940/49DF30: two statistics and per-lap scores.
            const auto& score=session.style().item(0);
            out["RacerName"]=name+" - "+car;
            out["RaceFinishedPosition"]="0"+std::to_string(session.player_position());
            out["DataName_1"]="BEST DRIFT SCORE:";
            out["Data_1"]=std::to_string(int(score.best));
            out["DataName_2"]="LONGEST DRIFT:";
            char duration[48];std::snprintf(duration,sizeof duration,"%.2f sec",score.longest);
            out["Data_2"]=duration;
            out["AllLapTimesTitle"]="ALL LAP SCORES";
            out["PositionAfterEachLapTitle"]="";
            for(unsigned lap=1;lap<positions_.size();++lap)if(positions_[lap]) {
                const auto key=std::to_string(lap);
                out["LapNumber_"+key]=key;
                out["AllLapTimes_"+key]=std::to_string(int(scores_[lap]));
                out["PositionAfterEachLap_"+key]=std::to_string(positions_[lap]);
            }
            return out;
        }
        const char* labels[]={"FINISH TIME:","TIME BEHIND:","BEST LAP TIME:",
            "TOP SPEED:","AVERAGE SPEED:","TRIP ODOMETER:","LAPS LED:"};
        for(unsigned i=0;i<7;++i)out["DataName_"+std::to_string(i+1)]=labels[i];
        out["RacerName"]=name+" - "+car;
        out["RaceFinishedPosition"]=(r.position<10?"0":"")+std::to_string(r.position);
        out["Data_1"]=r.finish_valid&&r.finish_ticks>0?format_race_ticks(r.finish_ticks):"--:--.--";
        std::int32_t winner=r.finish_ticks,best=kRaceNoTime;
        for(const auto& a:t.racers)if(a.finish_valid)winner=std::min(winner,a.finish_ticks);
        //49D5B0 uses terminal reason text in the time-behind field.
        if(r.status==RacerStatus::normal_completion)out["Data_2"]=r.finish_valid&&r.finish_ticks>0&&r.position!=1?
            post_race_time(r.finish_ticks-winner,true):"--:--.--";
        else out["Data_2"]=r.status==RacerStatus::knocked_out?"Knocked Out":r.status==RacerStatus::blown_engine?"Blown Engine":
            r.status==RacerStatus::totalled?"Totaled":r.status==RacerStatus::out_of_time?"Timeup":"Finished";
        if(session.is_drag()) {
            //49CC70 ->49D4C0/49D5B0/49DB30/49DE00/49DE80.
            out["DataName_3"]="TOP SPEED:";
            out["DataName_4"]="PERFECT SHIFTS:";
            out["DataName_5"]="PERFECT SHIFT %:";
            out["DataName_6"]=out["DataName_7"]="";
            char text[64];std::snprintf(text,sizeof text,"%.2f MPH",top_*2.237);
            out["Data_3"]=text;
            out["Data_4"]=std::to_string(driver.drag_perfect_shifts())+"/"+std::to_string(driver.drag_shifts());
            //49DE80 uses a binary32 fraction and one decimal, including0/0.
            const float percent=driver.drag_shifts()?float(driver.drag_perfect_shifts())/float(driver.drag_shifts())*100.F:0.F;
            std::snprintf(text,sizeof text,"%3.1f%%",percent);out["Data_5"]=text;
            out["AllLapTimesTitle"]=out["PositionAfterEachLapTitle"]="";
            return out;
        }
        const bool sprint=session.course().route.point_to_point;
        if(sprint) {
            const char* labels[]={"TOP SPEED:","AVERAGE SPEED:","TRIP ODOMETER:"};
            for(unsigned i=0;i<3;++i)out["DataName_"+std::to_string(i+3)]=labels[i];
            out["DataName_6"]=out["DataName_7"]="";
            char text[64];std::snprintf(text,sizeof text,"%.2f MPH",top_*2.237);out["Data_3"]=text;
            std::snprintf(text,sizeof text,"%.2f MPH",(seconds_?distance_/seconds_:0)*2.237);out["Data_4"]=text;
            std::snprintf(text,sizeof text,"%.2f MI",distance_*.000625);out["Data_5"]=text;
            out["AllLapTimesTitle"]=out["PositionAfterEachLapTitle"]="";return out;
        }
        unsigned led=0;
        for(unsigned lap=1;lap<positions_.size();++lap)if(r.lap_ticks[lap]>0) {
            best=std::min(best,r.lap_ticks[lap]);
            const auto index=std::to_string(lap);
            out["LapNumber_"+index]=index;
            out["AllLapTimes_"+index]=format_race_ticks(r.lap_ticks[lap]);
            out["PositionAfterEachLap_"+index]=std::to_string(positions_[lap]);
            if(positions_[lap]==1)++led;
        }
        out["Data_3"]=best==kRaceNoTime?"--:--.--":format_race_ticks(best);
        auto number=[](double value,const char* unit){char b[64];std::snprintf(b,sizeof b,"%.2f %s",value,unit);return std::string(b);};
        out["Data_4"]=number(top_*2.237,"MPH");
        out["Data_5"]=number((seconds_?distance_/seconds_:0)*2.237,"MPH");
        out["Data_6"]=number(distance_*.000625,"MI");
        out["Data_7"]=std::to_string(led);
        return out;
    }
private:
    struct Sample {
        bool done=false;
        double top=0,distance=0,seconds=0;
        std::array<unsigned,kRaceLapSlots> positions{};
        std::array<float,kRaceLapSlots> scores{};
        float last_score=0;
    };
    std::size_t last_step_=0;
    std::array<Sample,4> samples_{};
};
//4ACE64 clamps visual reputation to1;49F920 converts it to an integer
// before49F566 multiplies the category subtotal. Race points are not changed.
inline int post_race_style_multiplier(float reputation) {
    return std::isfinite(reputation)?int(std::clamp(reputation,1.F,5.F)):1;
}
inline int post_race_style_subtotal(const RaceSession& session) {
    int sum=0;
    for(unsigned i=session.is_drift()?1:0;i<kRaceStyleKinds;++i)
        if(session.style().rule(i).enabled)sum+=int(session.style().item(i).total);
    return sum;
}
// Bind the genuine PostRaceResults/StyleAnimator/RewardPopup object names.
inline bool post_race_result_ready(const RaceRacerTiming& result) noexcept {
    return result.finish_valid||result.status==RacerStatus::knocked_out||
        result.status==RacerStatus::blown_engine||result.status==RacerStatus::totalled||
        result.status==RacerStatus::out_of_time;
}
inline PostRaceValues post_race_report(const RaceSession& session,PostRaceValues values,
    const std::array<std::string,4>& car_labels,unsigned prize,std::uint32_t previous_style=0,float visual_reputation=0) {
    values["RaceMode"]=session.is_drag()?"drag":session.is_drift()?"drift":session.course().route.point_to_point?"sprint":"circuit";
    const auto rows=session.result_rows();
    for(unsigned n=0;n<4;++n) {
        const auto key=std::to_string(n+1);
        const bool drift=session.is_drift();
        const unsigned actor=drift?session.drift_competition().standings()[n].actor:n<rows.size()?unsigned(rows[n].racer):4;
        values["Pos"+key]=actor<rows.size()||drift?std::to_string(n+1):"";
        values["Name"+key]=actor==0?session.player_name():drift?session.drift_competition().standings()[n].name:
            actor<=session.rival_count()?session.rival_name(actor-1):"";
        values["CarModel_"+key]=actor<car_labels.size()?car_labels[actor]:"";
        values["BestLap_"+key]="";
        values["Time"+key]=values["Lead"+key]="";
        if(drift) {
            values["Time"+key]=std::to_string(int(session.drift_competition().standings()[n].score));
            values["Lead"+key]=values["Time"+key];
        }else if(n<rows.size()) {
            const auto& r=rows[n];
            const auto status=r.status==RacerStatus::blown_engine?"Blown Engine":
                r.status==RacerStatus::totalled?"Totaled":
                r.status==RacerStatus::knocked_out?"Knocked Out":r.status==RacerStatus::out_of_time?"Timeup":"Did Not Finish";
            values["Time"+key]=r.total_valid&&r.status==RacerStatus::normal_completion?post_race_time(r.total_ticks):status;
            values["Lead"+key]=values["Time"+key];
            if(n && r.total_valid && !rows.empty() && rows[0].total_valid)
                values["Lead"+key]=post_race_time(std::max(0,r.total_ticks-rows[0].total_ticks),true);
            if(r.best_lap_ticks!=kRaceNoTime && !session.is_drag())values["BestLap_"+key]=format_race_ticks(r.best_lap_ticks);
        }
        if(actor==0)values["PlayerRow"]=key;
    }
    int subtotal=0;
    for(unsigned i=session.is_drift()?1:0;i<kRaceStyleKinds;++i)if(session.style().rule(i).enabled && int(session.style().item(i).total)!=0) {
        const auto points=int(session.style().item(i).total);subtotal+=points;
        const auto key=std::to_string(i);
        values["StyleLabel"+key]=session.style().rule(i).label;
        values["StyleValue"+key]=std::to_string(points);
    }
    values["StyleCategory"]="";values["StyleCategoryPoints"]="";
    values["CountingStylePoints"]=std::to_string(subtotal);
    const int multiplier=post_race_style_multiplier(visual_reputation);
    values["StyleMultiplier"]=std::to_string(multiplier);values["TotalPointsAnimate"]=std::to_string(subtotal*multiplier);
    values["UserName"]=session.player_name();
    values["CareerStyleBase"]=std::to_string(previous_style);
    values["RewardBlurbString"]="Congratulations!  You have "+std::to_string(prize)+" added to your Bank.";
    values["Victory_Title"]="Race Victory Reward";
    return values;
}

inline void bind_post_race_tournament(PostRaceValues& values,const RaceSession& session,
    const CareerTournament& tournament,const std::array<unsigned,4>& places,
    const std::array<unsigned,4>& configuration_to_actor,const std::array<std::string,4>& labels) {
    std::array<std::string,4> names;
    if(session.is_drift()) {
        for(const auto& row:session.drift_competition().standings())names.at(row.actor)=row.name;
    }else {
        for(unsigned i=0;i<session.rival_count();++i)names.at(i+1)=session.rival_name(i);
    }
    names[0]=session.player_name();
    values["Tournament"]="1";
    const auto order=tournament.order(),totals=tournament.totals();
    for(unsigned row=0;row<4;++row) {
        const auto config=order[row],actor=configuration_to_actor[config];
        const auto key=std::to_string(row+1);
        values["Name"+key]=names[actor];values["CarModel_"+key]=labels[actor];values["Pos"+key]=key;
        if(!actor)values["PlayerRow"]=key;
        values["Time"+key]=values["Lead"+key]=values["BestLap_"+key]="";
        values["PointEarned_"+key]="Points Earned: "+std::to_string(10-2*places[config]);
        values["TotalPoints_"+key]="Total Points: "+std::to_string(totals[config]);
    }
}

//49F1B0/49F2B0 category cadence:0.5s pause, count to1.75s,
//hold until strictly after2.55s;49F403/49F490 multiplier/total stages.
class PostRaceStyleAnimator {
public:
    explicit PostRaceStyleAnimator(const PostRaceValues& values) {
        for(unsigned i=0;i<kRaceStyleKinds;++i) {
            const auto key=std::to_string(i);auto p=values.find("StyleValue"+key),l=values.find("StyleLabel"+key);
            if(p!=values.end()&&l!=values.end())items_.push_back({l->second,std::stoi(p->second)});
        }
        auto b=values.find("CareerStyleBase");if(b!=values.end())base_=std::stoll(b->second);
        for(const auto& p:items_)total_+=p.second;
        auto m=values.find("StyleMultiplier");if(m!=values.end())multiplier_=std::stoi(m->second);
    }
    void advance(double seconds) {if(std::isfinite(seconds)&&seconds>0&&seconds<=1)elapsed_+=seconds;}
    void finish() {elapsed_=duration();}
    bool done()const {return elapsed_>=duration();}
    void bind(PostRaceValues& values)const {
        const auto index=std::min(items_.size(),std::size_t(elapsed_/2.55));
        //49F120 restarts StyleCategory Flashy for each category.49F33A..
        //49F385 flash subtotal/new category or multiplier;49F470/49F5A5
        //restart TotalPointsAnimate. UI scripts use1200 ticks per second.
        const auto clock=[](double seconds){return std::to_string(unsigned(std::min(std::max(seconds,0.0)*1200,16777215.0)));};
        if(!items_.empty())values["CategoryFlashTick"]=clock(elapsed_-std::min(index,items_.size()-1)*2.55);
        if(index&&items_.size()>1)values["CountingFlashTick"]=clock(elapsed_-std::min(index,items_.size()-1)*2.55);
        if(index==items_.size()) {
            const auto t=elapsed_-items_.size()*2.55;
            values["MultiplierFlashTick"]=clock(t);
            if(t>.5)values["TotalFlashTick"]=clock(t-(t>=3.5?3.5:.5));
        }
        int sum=0;for(std::size_t i=0;i<index;++i)sum+=items_[i].second;
        values["StyleCategory"]=index<items_.size()?items_[index].first:items_.empty()?"":items_.back().first;
        values["StyleCategoryPoints"]=index<items_.size()?std::to_string(items_[index].second):items_.empty()?"":std::to_string(items_.back().second);
        if(index<items_.size()) {
            const auto ticks=std::int64_t(elapsed_*4000)-std::int64_t(index)*10200;
            const float t=float(ticks)*.00025F;
            const int part=t<=.5F?0:t<=1.75F?int(float(items_[index].second)*((t-.5F)/1.25F)+.5F):items_[index].second;
            values["StyleStage"]="0";values["CountingStylePoints"]=std::to_string(sum+part);
            values["CareerStyleDisplayed"]=std::to_string(base_);
        }else {
            const double t=elapsed_-items_.size()*2.55;
            values["StyleStage"]=t<=.5?"1":"2";
            values["CountingStylePoints"]=std::to_string(total_);
            const int awarded=total_*multiplier_;
            const int part=t<=1?0:t<3.5?int(float(awarded)*float((t-1)/2.5)+.5F):awarded;
            values["TotalPointsAnimate"]=std::to_string(part);
            values["CareerStyleDisplayed"]=std::to_string(std::max<std::int64_t>(0,base_+part));
        }
    }
private:
    double duration()const {return items_.size()*2.55+3.5;}
    std::vector<std::pair<std::string,int>> items_;
    double elapsed_=0;int total_=0,multiplier_=1;std::int64_t base_=0;
};

}
