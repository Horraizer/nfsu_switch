#include "nfsu/race_style.h"
#include "nfsu/race_events.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
namespace nfsu {
namespace {
std::uint32_t u32(const unsigned char* p) {return p[0]|std::uint32_t(p[1])<<8|std::uint32_t(p[2])<<16|std::uint32_t(p[3])<<24;}
float f32(const unsigned char* p) {auto bits=u32(p);float v;std::memcpy(&v,&bits,4);return v;}
double elapsed(std::int32_t now,std::int32_t then) {return double(std::int64_t(now)-then)*double(.00025F);}
}
bool RaceStyle::load(const std::filesystem::path& root,std::string& error,bool drift) {
    std::vector<unsigned char> image;std::vector<RaceTableIssue> issues;
    const RaceTableLimits limits{32ULL*1024*1024,32ULL*1024*1024,16384};
    RaceChunkPayload span;
    if(!load_race_image(root/"Global/GLOBALB.BUN",image,issues,limits) ||
       !find_race_chunk(image.data(),image.size(),0x34a07,span,issues,limits)) {
        error="style tuning chunk absent or invalid";return false;
    }
    span.begin=(span.begin+15)&~std::uint64_t(15);
    if(span.end<span.begin || span.end-span.begin!=32*kRaceStyleKinds) {error="style tuning size";return false;}
    std::ifstream file(root/"Languages/English.bin",std::ios::binary);
    std::vector<unsigned char> lang{std::istreambuf_iterator<char>(file),{}};
    if(lang.size()<24 || u32(lang.data())!=0x39000 || u32(lang.data()+4)>lang.size()-8) {error="style language header";return false;}
    const std::size_t end=8+u32(lang.data()+4),count=u32(lang.data()+12),table=8+u32(lang.data()+16),strings=8+u32(lang.data()+20);
    if(table>end || count>(end-table)/8 || strings>=end) {error="style language bounds";return false;}
    std::array<RaceStyleRule,kRaceStyleKinds> rules;
    for(unsigned i=0;i<kRaceStyleKinds;++i) {
        const auto* p=image.data()+span.begin+32*i;auto& r=rules[i];
        r.language_hash=u32(p);r.continuous=p[6]!=0;r.enabled=(p[7]&(drift?4:1))!=0;
        r.duration=f32(p+8);r.timeout=f32(p+12);r.minimum=f32(p+16);r.limit=f32(p+20);r.rate=f32(p+24);
        r.multiplier_limit=std::int8_t(p[28]);r.multiplier_step=std::int8_t(p[29]);r.count_limit=std::int8_t(p[30]);
        if(p[4]!=i || p[6]>1 || r.multiplier_limit<1 || r.multiplier_step<0 ||
           !std::isfinite(r.duration)||!std::isfinite(r.timeout)||!std::isfinite(r.minimum)||!std::isfinite(r.limit)||!std::isfinite(r.rate)||r.duration<0||r.timeout<0) {
            error="style tuning record refused";return false;
        }
        for(std::size_t j=0;j<count;++j) if(u32(lang.data()+table+8*j)==r.language_hash) {
            const auto offset=u32(lang.data()+table+8*j+4);if(offset>=end-strings)break;
            const auto first=strings+offset;auto last=first;
            while(last<end && lang[last] && last-first<63 && lang[last]>=32 && lang[last]<127)++last;
            if(last<end && !lang[last])r.label.assign(reinterpret_cast<const char*>(lang.data()+first),last-first);
            break;
        }
        if(r.label.empty()){error="style localized label absent";return false;}
    }
    configure(rules,drift);error.clear();return true;
}
void RaceStyle::configure(const std::array<RaceStyleRule,kRaceStyleKinds>& rules,bool drift) {
    rules_=rules;items_={};notices_={};retired_notice_.reset();drift_publication_={};count_=0;total_=0;ready_=true;drift_=drift;
}
double RaceStyle::accrue(unsigned i,float seconds) noexcept {
    auto& s=items_[i];const auto& r=rules_[i];
    double result=double(s.multiplier)*r.rate*s.factor*seconds+s.points;
    if((r.limit>0 && result>=r.limit)||(r.limit<0 && result<r.limit))result=r.limit;
    const double raw=double(r.rate)*s.factor*seconds;
    s.raw=float(s.raw+std::min(result,raw));return result;
}
void RaceStyle::trigger(unsigned i,float factor,std::int32_t now) noexcept {
    if(!ready_ || i>=kRaceStyleKinds || !rules_[i].enabled || !std::isfinite(factor))return;
    auto& s=items_[i];const auto& r=rules_[i];
    if((r.count_limit>=0 && s.count>=r.count_limit)||s.phase==3||s.phase==4)return;
    s.factor=factor;s.touched=now;
    if(!r.continuous) {
        const auto value=accrue(i,1);
        if((r.rate<=0 && value<r.minimum)||(r.rate>0 && value>r.minimum)) {
            s.points=float(value);s.total=float(double(s.total)+s.points);s.phase=3;
        }
    } else if(s.phase==2) {s.began=now;s.phase=1;}
}
void RaceStyle::break_chain(unsigned i) noexcept {if(i<kRaceStyleKinds)items_[i].multiplier=1;}
void RaceStyle::set_multiplier(unsigned i,int value) noexcept {
    if(i<kRaceStyleKinds && value>=1 && value<=5)items_[i].multiplier=value;
}
void RaceStyle::cancel(unsigned i) noexcept {
    if(i>=kRaceStyleKinds)return;
    auto& s=items_[i];
    if(s.phase==0||s.phase==1) {if(s.phase==1)s.points=s.raw=0;s.phase=4;}
}
void RaceStyle::cancel_active() noexcept {
    for(auto& s:items_)if(s.phase==0||s.phase==1) {
        if(s.phase==1){s.points=0;s.raw=0;}s.phase=4;
    }
}
void RaceStyle::finish(std::int32_t now) noexcept {
    for(unsigned i=0;i<kRaceStyleKinds;++i) {
        auto& s=items_[i];if(s.phase==0){s.total=float(double(s.total)+s.points);s.phase=3;}
        else if(s.phase==1){s.points=0;s.raw=0;s.phase=4;}
    }
    update(now,0);
}
void RaceStyle::publish(unsigned i,std::int32_t now) noexcept {
    const auto& s=items_[i];
    if(drift_&&i==0) {
        drift_publication_.publish(s.points,s.multiplier,s.phase==4,now);return;
    }
    if(s.points==0)return;
    unsigned at=0;while(at<count_ && notices_[at].kind!=i)++at;
    if(at==count_) {
        if(count_==notices_.size())return;
        // Positive events append; negatives are inserted first, as 4A85E0.
        if(s.points<0) {for(unsigned n=count_;n>0;--n)notices_[n]=notices_[n-1];at=0;}
        ++count_;
        notices_[at]={i,s.points,s.multiplier,now,rules_[i].continuous,s.phase==4,false};
    } else {
        auto& n=notices_[at];n.points=s.points;n.failed=s.phase==4;
        if(s.phase!=3)n.multiplier=s.multiplier;
        n.started=false;
    }
}
void RaceStyle::update(std::int32_t now,float seconds) noexcept {
    if(!ready_)return;
    retired_notice_.reset();
    float banked=0;
    //43DE72 clears item0+15 in drift;436A28 excludes it from general style.
    for(unsigned i=drift_?1:0;i<items_.size();++i)
        banked=float(double(banked)+std::trunc(items_[i].total));
    total_=int(std::clamp(double(banked),-2147483647.,2147483647.));
    // Original publication/reset precedes the timeout/accrual pass.
    for(unsigned i=0;i<kRaceStyleKinds;++i) {
        auto& s=items_[i];const auto& r=rules_[i];
        if(s.phase==0||s.phase==3||s.phase==4)publish(i,now);
        if(s.phase==3||s.phase==4) {
            if(s.phase==3) {++s.count;if(s.multiplier<r.multiplier_limit)s.multiplier+=r.multiplier_step;
                s.longest=std::max(s.longest,float(elapsed(now,s.began)));s.best=std::max(s.best,s.points);}
            s.points=s.raw=0;s.began=0;s.phase=2;
        }
        const float since=float(elapsed(now,s.touched));
        if(since>r.timeout) {
            if(s.phase==0){s.total=float(double(s.total)+s.points);s.phase=3;}
            else if(s.phase==1){s.points=s.raw=0;s.began=0;s.phase=2;}
        }
        if((s.phase==0||s.phase==1) && double(seconds)*2>since)s.points=float(accrue(i,seconds));
        if(s.phase==1 && elapsed(now,s.began)>r.duration &&
           ((r.rate<=0 && s.raw<r.minimum)||(r.rate>0 && s.raw>r.minimum))) {++s.count;s.phase=0;}
    }
    if(count_) {
        unsigned at=0;while(at<count_ && notices_[at].continuous)++at;if(at==count_)at=0;
        auto& n=notices_[at];if(!n.started){n.started=true;n.shown=now;}
        // A continuous drift notice stays queued until a HUD frame shows
        // it. Simulation catch-up can update the queue several times first.
        if(!(drift_&&n.continuous)&&elapsed(now,n.shown)>(count_>1?.5:1.)) {
            retired_notice_=n;
            erase_notice(at);
        }
    }
}
int RaceStyle::total() const noexcept {
    return total_;
}
const RaceStyleNotice* RaceStyle::notice() const noexcept {
    if(retired_notice_)return &*retired_notice_;
    if(!count_)return nullptr;
    for(unsigned i=0;i<count_;++i)if(!notices_[i].continuous)return &notices_[i];
    return &notices_[0];
}
void RaceStyle::erase_notice(unsigned at) noexcept {
    for(unsigned j=at+1;j<count_;++j)notices_[j-1]=notices_[j];
    --count_;
}
void RaceStyle::consume_drift_notice() noexcept {
    // 4A83C0 unlinks the continuous item after its display update. If a
    // discrete notice is selected, no continuous item was displayed.
    const auto* shown=notice();
    if(drift_&&shown&&shown->continuous)erase_notice(unsigned(shown-notices_.data()));
}
float race_drafting_pair(const std::array<float,3>& p,const std::array<float,3>& v,
                         float speed,const std::array<float,3>& follower) noexcept {
    if(!(speed>1))return 0;
    const float x=p[0]-follower[0],y=p[1]-follower[1],z=p[2]-follower[2];
    const float distance=std::sqrt((x*x+z*z)+y*y);
    if(!(distance>0)||distance>=60)return 0;
    const float length=std::sqrt((v[0]*v[0]+v[1]*v[1])+v[2]*v[2]);
    if(!(length>0))return 0;
    const float cosine=((v[2]/length*z+v[1]/length*y)+x*(v[0]/length))/distance;
    const float cone=(cosine-.949999988079071F)/(1-.949999988079071F);
    if(!(cone>0))return 0;
    // 7062B4: four nonuniform speed/range points.
    const float reach=speed<=1?0:speed<5?10*((speed-1)/4):
        speed<150?50*((speed-5)/145)+10:60;
    const float range=reach*cone;if(!(range>9.999999747378752e-6F))return 0;
    const float u=std::clamp(distance/range,0.F,1.F)*5;
    constexpr float curve[]={1,.8999999761581421F,.75F,.550000011920929F,.30000001192092896F,0};
    const unsigned i=unsigned(u);if(i>=5)return curve[5];
    const float f=u-float(i);return (1-f)*curve[i]+f*curve[i+1];
}
void RaceStyleNearMiss::update(RaceStyle& score,std::int32_t now,float speed,
                              const RaceNearPass* pairs,std::size_t count) noexcept {
    if(speed<=20.116228103637695F||(have_impact_&&elapsed(now,impact_at_)<=.5)) {candidate_=0;return;}
    if(candidate_) {
        const RaceNearPass* found=nullptr;
        for(std::size_t i=0;i<count;++i)if(pairs[i].identity==candidate_){found=pairs+i;break;}
        if(found&&found->contact){candidate_=0;return;}
        if(!found||found->hull_distance>1.5F*1.25F){score.trigger(22,1,now);candidate_=0;}
        return;
    }
    for(std::size_t i=0;i<count;++i)if(pairs[i].identity&&!pairs[i].contact&&
        pairs[i].hull_distance<=1.5F&&pairs[i].relative_speed>40.23245620727539F){candidate_=pairs[i].identity;break;}
}
void RaceStyleDriving::update(RaceStyle& score,const RaceStyleDrivingSample& s) noexcept {
    if(!score.ready() || !s.racing || finished_)return;
    if(s.impact) {
        section_=lap_=clean_=false;impact_at_=s.now;
        score.break_chain(22);
    }
    // 43F430: timing booleans are edge-triggered, including the initial line.
    if(s.new_checkpoint && !checkpoint_edge_) {
        if(section_)score.trigger(17,1,s.now);
        else {section_=true;score.break_chain(17);}
    }
    if(s.new_lap && !lap_edge_) {
        if(lap_)score.trigger(18,1,s.now);
        else {lap_=true;score.break_chain(18);}
        if(s.position==1)score.trigger(16,1,s.now);else score.break_chain(16);
    }
    checkpoint_edge_=s.new_checkpoint;lap_edge_=s.new_lap;
    if(s.finished) {
        if(clean_)score.trigger(19,1,s.now);
        if(s.position==1)score.trigger(13,1,s.now);
        score.finish(s.now);finished_=true;return;
    }
    // 43F8B0: no handbrake requirement; the published velocity/heading matters.
    if(s.speed>25 && s.support==4 && s.forward_speed>1 && std::abs(s.slip_degrees)>5)
        score.trigger(11,float(std::min(1.,double(s.speed)*std::abs(s.slip_degrees)/20)),s.now);
    const bool hanging=score.item(12).phase==0;
    if(!s.support && s.ground_valid && s.height>.75F && (s.up_z>.8F||hanging) &&
       (elapsed(s.now,impact_at_)>.5||hanging))score.trigger(12,1,s.now);
    // 43F9B0: two wrapped half-turns after acquisition, grounded throughout.
    if(s.support!=4 || std::abs(s.yaw_rate)<=1)spin_=0;
    else if(!spin_) {
        if(std::abs(s.yaw_rate)>2){spin_=1;spin_heading_=s.heading_degrees;}
    } else if(spin_==3) {score.trigger(25,1,s.now);spin_=0;}
    else {
        float delta=s.heading_degrees-spin_heading_;
        if(delta>180)delta-=360;else if(delta< -180)delta+=360;
        if(std::abs(delta)>=170){++spin_;spin_heading_=s.heading_degrees;}
    }
    if(s.racers>1 && s.race_seconds>1 && s.race_seconds<=1.5F && s.position==1)
        score.trigger(28,1,s.now);
    if(s.speed>20 && s.drafting>.25F)score.trigger(26,s.drafting,s.now);
    if(s.shortcut)score.trigger(20,1,s.now);
    score.update(s.now,s.seconds);
}
}
