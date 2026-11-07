#include "nfsu/collision_audio.h"
#include "nfsu/traffic_audio.h"
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
    //530F80/531030 publish binary32 curve output, then674898 truncates.
    //Graph product/lookup rounding remains separately nearest-even.
    const float fraction=std::clamp(float(x)/float(upper),0.F,1.F);
    return int(fraction*127.F);
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

int car_collision_subtype(VehicleVec3 a,VehicleVec3 b,VehicleVec3 va,
    VehicleVec3 vb,VehicleVec3 point) noexcept {
    // 531AC0: parallel horizontal velocity, then either contact-arm angle.
    // The port supplies its existing SAT point, not original GJK admission.
    const auto unit=[](VehicleVec3 v){
        const double n=length(v);
        if(!(n>0)||!std::isfinite(n))return VehicleVec3{1,0,0};
        return VehicleVec3{float(v.x/n),float(v.y/n),float(v.z/n)};
    };
    const auto dot=[](VehicleVec3 x,VehicleVec3 y){return (x.x*y.x+x.y*y.y)+x.z*y.z;};
    if(std::abs(dot(unit({va.x,va.y,0}),unit({vb.x,vb.y,0})))<=double(0.7070000171661377F))return 0;
    return std::abs(dot(unit(va),unit({point.x-a.x,point.y-a.y,point.z-a.z})))<=double(0.8F) ||
           std::abs(dot(unit(vb),unit({point.x-b.x,point.y-b.y,point.z-b.z})))<=double(0.8F);
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

int collision_scrape_intensity(double slide) noexcept {return intensity(slide,40);}

int prop_collision_intensity(float impulse,float inverse_mass) noexcept {
    const double delta=double(impulse)*inverse_mass;
    return std::isfinite(delta)?int(std::clamp(delta/21.0,0.0,1.0)*127):0;
}
unsigned prop_collision_variant(unsigned material,std::array<unsigned,29>& counters) noexcept {
    //52F7B0: materials share the original per-family cycling counters.
    constexpr unsigned base[]={13,0,11,28,3,22,6,13,13,16,16,16,8,13,13,25,19};
    constexpr unsigned count[]={3,3,2,1,3,3,2,3,3,3,3,3,3,3,3,3,3};
    if(material>=std::size(base))return 8;
    const auto b=base[material];counters[b]=(counters[b]+1)%count[material];return b+counters[b];
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

bool collision_world_admitted(const CollisionAdmissionRecord& current,
    const std::array<float,3>& previous_direction,const std::array<float,3>& previous_point,
    const CollisionAdmissionRecord* attached,std::size_t count) noexcept {
    if(current.rejected)return false;
    if(!current.actor)return true;
    const auto norm=[](const auto& v){return std::sqrt(double(v[0])*v[0]+double(v[1])*v[1]+double(v[2])*v[2]);};
    const auto unit=[&](const auto& v){const float length=float(norm(v));std::array<float,3> r{{1,0,0}};
        if(length!=0){const float inverse=1.F/length;for(unsigned j=0;j<3;++j)r[j]=v[j]*inverse;}return r;};
    const auto dot=[](const auto& a,const auto& b){return double(a[0])*b[0]+double(a[1])*b[1]+double(a[2])*b[2];};
    const auto scale=[](auto v,float factor){for(auto& x:v)x*=factor;return v;};
    const auto current_unit=unit(current.direction),previous_unit=unit(previous_direction);
    std::array<float,3> separation{};for(unsigned j=0;j<3;++j)separation[j]=current.point[j]-previous_point[j];
    if(dot(current_unit,previous_unit)>0.95 && norm(scale(current.direction,0.95F))<norm(previous_direction) &&
        norm(previous_direction)<norm(scale(current.direction,1.05F)) && norm(separation)<1.0 && std::abs(current.direction[2])>0.707)return false;
    for(std::size_t i=0;i<count;++i){const auto& other=attached[i];
        if(!other.attached||other.subtype!=5||!other.actor||other.actor!=current.actor)continue;
        if(other.id==current.id){if(other.started)return false;}
        else if(std::abs(dot(current.direction,other.direction))>0.8)return false;
    }
    return true;
}

std::vector<CollisionQueueCommand> collision_impact_queue(const std::vector<CollisionQueueRecord>& pending,
    const std::vector<CollisionQueueRecord>& attached,unsigned capacity,unsigned active_count) {
    std::vector<CollisionQueueCommand> out;
    if(pending.empty())return out;
    if(active_count==UINT32_MAX)active_count=unsigned(attached.size());
    if(pending.size()+active_count<=capacity) {
        for(const auto& r:pending)out.push_back({true,r.id});
        return out;
    }
    // The original over-capacity traversal appends every priority record and
    // only the closest nonpriority record from each list. Do not replace this
    // with a conventional full distance sort: the full52FB60 witness differs.
    const auto selected=[](const auto& records) {
        std::vector<CollisionQueueRecord> list;const CollisionQueueRecord* closest=nullptr;
        for(const auto& r:records)if(r.priority)list.push_back(r);
        else if(!closest||r.distance<closest->distance)closest=&r;
        if(closest)list.push_back(*closest);
        return list;
    };
    const auto requests=selected(pending),playing=selected(attached);bool replaced=false;
    for(std::size_t i=0;i<requests.size()&&i<playing.size();++i) {
        const auto& p=requests[i];const auto& a=playing[playing.size()-1-i];
        const bool replace=p.priority?!a.priority:!a.priority&&p.distance<a.distance;
        if(replace){out.push_back({false,a.id});out.push_back({true,p.id});replaced=true;}
        else if(!replaced||(!p.priority&&a.priority))break;
    }
    return out;
}

std::uint32_t CollisionScheduler::random_word() noexcept {
    return (random_context_?*random_context_:process_bank_audio_random()).next();
}
bool CollisionScheduler::start_impact(CollisionVoice& voice) {
    for(auto& p:pairs_)if(p.used&&p.id==voice.pair&&p.episode==voice.episode) {
        if(p.started)return false;
        unsigned variant=0;
        if(p.subtype<2) {
            if(p.subtype==1){car_four_=(car_four_+1)%4;variant=5+car_four_;}
            else{car_three_=(car_three_+1)%3;variant=9+car_three_;}
        }else if(p.subtype==2||p.subtype==5||p.subtype==6){family_four_=(family_four_+1)%4;variant=5+family_four_;}
        else if(p.subtype==3){family_land_=(family_land_+1)%3;variant=family_land_;}
        else if(p.subtype==4){family_three_=(family_three_+1)%3;variant=9+family_three_;}
        p.variant=variant;p.jitter=int(random_word()%301)-150;p.started=true;
        previous_direction_=p.direction;previous_point_=p.point;
        voice.sample=30+variant;voice.pitch_q16=pitch(voice.base_pitch+p.jitter);return true;
    }
    return false;
}
void CollisionScheduler::clear() noexcept {
    car_four_=car_three_=0;previous_direction_={};previous_point_={};
    pairs_ = {}; family_four_ = family_land_ = family_three_ = 0;
    have_tick_ = false; last_tick_ = last_physics_step_ = 0;
}

CollisionAudioFrame CollisionScheduler::update(std::uint64_t tick, std::uint64_t physics_step,
    const CollisionContact* contacts, std::size_t count, const CollisionGraph& g, bool reset,int category_gain,const CollisionSpatialFrame* spatial,bool defer_impact_start) {
    CollisionAudioFrame out;
    if(category_gain<0||category_gain>32768||(spatial&&(spatial->impact_global_gain<0||spatial->impact_global_gain>32768)))return out;
    if (have_tick_ && tick == last_tick_) return out;
    if (reset || (have_tick_ && (tick < last_tick_ || physics_step < last_physics_step_))) clear();
    // Countdown and held physics do not advance contact episodes or graph
    // controls. Remember the race tick so a repeated reset is also idempotent.
    const bool held = have_tick_ && physics_step == last_physics_step_;
    have_tick_ = true; last_tick_ = tick; last_physics_step_ = physics_step;
    if (held) return out;
    out.processed = true;
    count = std::min(count,kCollisionContactFrameLimit);
    if (!contacts) count = 0;
    // A reset snapshot discards pre-reset contacts from the same tick.
    if (reset) count = 0;
    const std::uint64_t newest = physics_step;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& c = contacts[i];
        if ((c.other_kind != 0 && c.other_kind != 3 && c.other_kind != 4) || !c.step || c.step > newest) continue;
        Pair* pair = nullptr;
        for (auto& p : pairs_) if (p.used && p.id == c.pair) { pair = &p; break; }
        if (!pair) for (auto& p : pairs_) if (!p.used || newest >= p.last_step+52) {
            p = {}; p.used = true; p.id = c.pair; pair = &p; break;
        }
        //465830 accumulates EVERY solver response, including multiple
        //witnesses of one pair in the same physical slice. Frame/held-clock
        //deduplication above already rejects repeated snapshots.
        if (!pair || c.step < pair->last_step) continue;
        const bool begin = !pair->last_step || c.step >= pair->last_step+36;
        if (begin) {
            const auto id = pair->id;
            *pair = {}; pair->used = true; pair->id = id;
            pair->pitch_target=g.scrape_pitch_targets[0]; pair->gain_target=g.scrape_gain_targets[0];
        }
        if(c.point_valid) {
            pair->point={float(c.point.x),float(c.point.y),float(c.point.z)};pair->point_valid=true;
        }
        pair->last_step = c.step;
        pair->slide=std::isfinite(c.slide_mps)?std::max(0.0,c.slide_mps):0;
        const double mag = length(c.impulse);
        const int subtype = c.other_kind==0?std::clamp(c.car_subtype,0,1):world_collision_subtype(c);
        if (subtype >= 0 && mag > 0) {
            // A zero-impulse overlap is not a fresh solver hit. Original
            // records retire after 300ms without a hit, even if the bodies
            // remain neighbors. Do not extend impact admission with silence.
            const bool impact_start = !pair->impacting || c.step>=pair->impact_step+36;
            if(impact_start){pair->sum=0;pair->episode=c.step;pair->started=false;pair->rejected=false;}
            const float dx=float(c.impulse.x),dy=float(c.impulse.y),dz=float(c.impulse.z);
            const float direction_length=std::sqrt(dx*dx+dy*dy+dz*dz);
            const float inverse=direction_length?1.F/direction_length:0.F;
            pair->direction=direction_length?std::array<float,3>{{dx*inverse,dy*inverse,dz*inverse}}:std::array<float,3>{{1,0,0}};
            if(impact_start)pair->attached_direction=pair->direction;
            pair->sum+=mag;pair->impact_step=c.step;
            if (impact_start) {
            pair->subtype=subtype;pair->impacting=true;
            if(!defer_impact_start) {
            unsigned variant = 0;
            if(c.other_kind==0) {
                if(subtype==1){car_four_=(car_four_+1)%4;variant=5+car_four_;}
                else {car_three_=(car_three_+1)%3;variant=9+car_three_;}
            } else if (subtype == 2 || subtype == 5 || subtype == 6) {
                family_four_ = (family_four_+1)%4; variant = 5+family_four_;
            } else if (subtype == 3) {
                family_land_ = (family_land_+1)%3; variant = family_land_;
            } else if (subtype == 4) {
                family_three_ = (family_three_+1)%3; variant = 9+family_three_;
            }
            pair->variant = variant; pair->subtype = subtype;
            pair->jitter = int(random_word()%301)-150; pair->impacting = true;pair->started=true;
            }
            }
            if(defer_impact_start&&!pair->started&&!pair->rejected&&c.other_kind!=0&&pair->output_tick!=tick) {
                CollisionAdmissionRecord current;current.id=pair->id;current.actor=1;current.subtype=pair->subtype;
                current.direction=pair->direction;current.point=pair->point;current.rejected=pair->rejected;
                std::array<CollisionAdmissionRecord,256+kRaceRivalCapacity+kRaceTrafficCapacity> attached{};std::size_t used=0;
                for(const auto& other:pairs_)if(other.used&&newest<other.last_step+52) {
                    auto& record=attached[used++];record.id=other.id;record.actor=1;record.subtype=other.subtype;
                    record.direction=other.attached_direction;record.started=other.started;
                }
                if(!collision_world_admitted(current,previous_direction_,previous_point_,attached.data(),used))pair->rejected=true;
            }
            // Refusal latches the impact only. The independent scrape graph
            // below continues exactly as it did before admission was connected.
            if(defer_impact_start&&pair->rejected)continue;
            const int ix = collision_intensity(pair->sum,pair->subtype);
            constexpr int gain[]{32767,22500,22000,20000,32767,22000,20000};
            CollisionAudioPosition position;const bool positional=spatial&&pair->point_valid;
            if(positional&&!position.tick(pair->point,spatial->actor,spatial->eye,spatial->forward))continue;
            //531350/530740: spatial, category7,5365E0 global, then bank.
            //531080's separate scrape path omits the impact-global stage.
            const int located=int((std::int64_t(gain[pair->subtype])*(positional?position.gain:32768))>>15);
            const int category=int((std::int64_t(located)*category_gain)>>15);
            const int configured=int((std::int64_t(category)*(spatial?spatial->impact_global_gain:32768))>>15);
            const int volume = nearest(float(double(std::max(0,int(g.impact_gain[ix])))*configured/32768));
            CollisionVoice voice{c.pair,30+pair->variant,std::uint32_t(volume),
                pitch(4096+g.impact_pitch[ix]+pair->jitter),defer_impact_start?!pair->started:impact_start};
            voice.episode=pair->episode;voice.base_pitch=4096+g.impact_pitch[ix];voice.audio_order=c.audio_order;
            if(spatial&&pair->point_valid) {
                float actor_distance=0,eye_distance=0;
                for(unsigned j=0;j<3;++j){const float x=pair->point[j]-spatial->actor[j],y=pair->point[j]-spatial->eye[j];actor_distance+=x*x;eye_distance+=y*y;}
                voice.admission_distance=std::min(std::sqrt(actor_distance),std::sqrt(eye_distance));
            }
            voice.positional=positional;voice.pan=std::uint16_t(position.pan);
            if(pair->output_tick==tick&&(defer_impact_start||pair->output_step==c.step)&&pair->output_index<out.impacts.size()){
                voice.start|=out.impacts[pair->output_index].start;
                voice.audio_order=out.impacts[pair->output_index].audio_order;
                out.impacts[pair->output_index]=voice;
            }else{
                pair->output_tick=tick;pair->output_step=c.step;pair->output_index=out.impacts.size();
                out.impacts.push_back(voice);
            }
        }
    }
    // Last observation for each pair drives the continuous graph once per
    // audio tick; physics slices do not advance oscillators separately.
    for (auto& p : pairs_) {
        if (!p.used || newest >= p.last_step+52) { p.used = false; continue; }
        if (!(p.slide > 0)) { p.scraping=false; continue; }
        const double age=double(newest-p.last_step)/120.0;
        const double release=std::clamp(1.0-(age-0.3)/double(0.13300000131130219F),0.0,1.0);
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
            p.pitch_target=g.scrape_pitch_targets[p.pitch_deck.next(random_word())];
        if (oscillator(p.gain_phase,p.gain_high,2000))
            p.gain_target=g.scrape_gain_targets[p.gain_deck.next(random_word())];
        // Before the first rising edge, deck output is index zero.
        const int pitch_mod = ramp(p.pitch_value,p.pitch_delta,p.pitch_last,p.pitch_target,261);
        const int gain_mod = ramp(p.gain_value,p.gain_delta,p.gain_last,p.gain_target,370);
        const int ix = collision_scrape_intensity(p.slide);
        CollisionAudioPosition position;const bool positional=spatial&&p.point_valid;
        if(positional&&!position.tick(p.point,spatial->actor,spatial->eye,spatial->forward))continue;
        const int located=int((INT64_C(20000)*(positional?position.gain:32768))>>15);
        const int configured=int((std::int64_t(located)*category_gain)>>15);
        const int base = nearest(float(double(std::max(0,int(g.scrape_gain[ix])))*configured/32768));
        // Template float factor 0x38000100, close to 1/32767.
        const int volume = nearest(double(base)*std::clamp(gain_mod,0,32767)*double(0x1.0002p-15F)*release);
        out.scrapes.push_back({p.id,1,std::uint32_t(volume),pitch(g.scrape_pitch[ix]+pitch_mod),start,std::uint16_t(position.pan),positional});
    }
    return out;
}
} // namespace nfsu::audio
