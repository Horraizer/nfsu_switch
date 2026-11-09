#include "nfsu/career.h"
#include "nfsu/career_ride.h"
#include "nfsu/car_assets.h"
#include "nfsu/texture.h"
#include "nfsu/sha256.h"
#include "nfsu/ai_powertrain.h"
#include "nfsu/frontend_preset.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace nfsu { namespace {
unsigned u32(const unsigned char* p) {
    return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);
}
std::string movie_name(const unsigned char* p) {
    std::string s;
    for(unsigned i=0;i<12;++i) {
        const auto c=p[i];if(!c)return s;
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))
            throw std::runtime_error("invalid original career movie name");
        s+=char(c);
    }
    throw std::runtime_error("unterminated career movie name");
}
#include "career_stats.inc"
bool paint_job_owned(const CareerProfile& p)noexcept {return p.ride.owned[0][0]!=0;}
bool refuse(std::string& error,const char* why){error=why;return false;}
}
#include "career_info.inc"
std::array<unsigned,4> CareerTournament::totals()const noexcept {
    constexpr unsigned points[]={0,8,6,4,2};std::array<unsigned,4> result{};
    for(unsigned r=0;r<std::min(round,unsigned(places.size()));++r)for(unsigned actor=0;actor<4;++actor)
        if(places[r][actor]>=1&&places[r][actor]<=4)result[actor]+=points[places[r][actor]];
    return result;
}
std::array<unsigned,4> CareerTournament::order()const noexcept {
    const auto points=totals();std::array<unsigned,4> order{{0,1,2,3}};
    //Original5A2AB0 swaps strictly lower totals. Equal totals stay in roster order.
    for(unsigned i=1;i<4;++i)for(unsigned j=i;j>0&&points[order[j-1]]<points[order[j]];--j)
        std::swap(order[j-1],order[j]);
    return order;
}
bool CareerCatalog::load(const std::filesystem::path& root,std::string& error) {
    try {
        CareerCatalog next;std::vector<unsigned char> image,global;std::vector<RaceTableIssue> issues;
        next.events_=load_race_event_table(root/"FrontEnd/FrontB.lzc",image);
        if(!next.events_.ok())return refuse(error,"career event table unavailable");
        if(!load_race_image(root/"Global/GLOBALB.BUN",global,issues))return refuse(error,"career car table unavailable");
        const auto cars=parse_car_type_table(global.data(),global.size());
        if(!cars.ok())return refuse(error,"career car table malformed");
        if(sha256_hex(sha256(image.data(),image.size()))!="94A714D1E8E3BEF7F77AEDCEE4FDC6350E285B06059B92D392687710E64E5D77" ||
           sha256_hex(sha256(global.data(),global.size()))!="39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9")
            return refuse(error,"career retail identity mismatch");
        const auto info_tracks=parse_track_info_table(global.data(),global.size());
        if(!info_tracks.ok())return refuse(error,"career statistics track table unavailable");
        for(const auto& track:info_tracks.records)next.statistics_track_masks_[track.number]=track.mode_mask;
        RaceChunkPayload ranks;
        if(!find_race_chunk(image.data(),image.size(),0x34a03,ranks,issues)||ranks.end-ranks.begin!=6400)
            return refuse(error,"original ranking table bounds");
        const auto* ranking=image.data()+ranks.begin;
        for(unsigned i=0;i<next.ranking_names_.size();++i)if(u32(ranking+12*i)==i) {
            const auto* name=ranking+12*i+4;unsigned n=0;
            while(n<8&&name[n]){if(name[n]<32||name[n]>126)return refuse(error,"original rank name invalid");++n;}
            if(n==8)return refuse(error,"original rank name unterminated");
            next.ranking_names_[i]=std::string(reinterpret_cast<const char*>(name),n);
        }
        for(unsigned i=0;i<next.ranking_rosters_.size();++i) {
            const auto* row=ranking+0x300+44*i;
            if(u32(row)!=i&&u32(row)!=0xffffffffU)return refuse(error,"original ranking event identity");
            for(unsigned n=0;n<40;++n) {
                const auto key=row[4+n];if(key>=62)return refuse(error,"ranking driver outside name table");
                next.ranking_rosters_[i][n]=key;
            }
        }
        for(const auto& event:next.events_.records) {
            const auto* raw=image.data()+event.offset;auto& m=next.media_[event.number];
            next.ranking_event_[event.number]={{std::int16_t(raw[0x34]|unsigned(raw[0x35])<<8),
                std::int16_t(raw[0x36]|unsigned(raw[0x37])<<8)}};
            m.before=movie_name(raw+0xac);m.after=movie_name(raw+0xc4);
            if(event.type==unsigned(RaceType::time_trial)&&(event.kind==2||event.kind==3)) {
                for(unsigned d=0;d<3;++d) {
                    std::memcpy(&m.deadlines[d],raw+0x1c+4*d,4);
                    if(!std::isfinite(m.deadlines[d])||m.deadlines[d]<=0)return refuse(error,"challenge deadline invalid");
                }
            }
            if(event.kind==2||event.kind==3) {
                const auto count=u32(raw+0x74);if(count>5)return refuse(error,"challenge reward bounds");
                for(unsigned i=0;i<count;++i)if(u32(raw+0x38+12*i)==0&&u32(raw+0x40+12*i)==4)
                    m.unique_categories.push_back(u32(raw+0x3c+12*i));
            }
            const auto reward_count=u32(raw+0x74);if(reward_count>5)return refuse(error,"magazine reward count invalid");
            for(unsigned i=0;i<reward_count;++i)if(u32(raw+0x38+12*i)==1)m.magazine_rewards.push_back(u32(raw+0x3c+12*i));
            //4C5AC3/4C55F0: the record holds a portrait hash, not a race-type default.
            m.portrait=u32(raw+0x130);
            //4C54B0..4C5550 walks this ordered table; duplicate event rows
            //must preserve the first match, and speech0 is a valid request.
            constexpr int spoken[][2]={{78,13},{27,0},{43,1},{57,2},{94,3},{110,4},{3,6},{13,7},{25,8},{57,9},{93,-1},{109,11},{1,14},{16,15},{19,16},{45,17},{59,18},{59,19},{80,20},{95,21},{7,23},{26,25},{68,52},{85,29},{97,31},{4,33},{11,34},{21,35},{46,36},{62,37},{76,38},{32,40},{37,41},{75,42},{96,43},{107,44},{6,46},{15,47},{47,52},{112,61},{77,59},{29,90},{36,91},{79,92},{108,93}};
            for(const auto& row:spoken)if(unsigned(row[0])==event.number){m.speech=row[1];break;}
            if(event.number==7)m.reward_speech=24;
            if(event.number==15)m.reward_speech=48;
            if(event.number==26)m.reward_speech=26;
            if(event.number==47||event.number==68)m.reward_speech=53;
            if(event.number==77)m.reward_speech=60;
            if(event.number==97)m.reward_speech=32;
            switch(m.portrait) {
            case 0x9dbcd681U: m.sender="Jose";break;
            case 0x9db972a3U: m.sender="Dirt";break;
            case 0x26cbb5bU: m.sender="Klutch";break;
            case 0x4fdc36fdU: m.sender="Samantha";break;
            case 0x1784e6a4U: m.sender="TJ";break;
            case 0x9db8dfc0U: m.sender="Chad";break;
            case 0x9dc2505bU: m.sender="Todd";break;
            case 0x9dbd7c56U: m.sender="Kurt";break;
            case 0x54f6e4ebU: m.sender="Eddie";break;
            default:break;
            }
            for(unsigned i=0;i<3;++i) {
                float reward;const auto bits=u32(raw+0x28+4*i);std::memcpy(&reward,&bits,4);
                if(!std::isfinite(reward)||reward<0||double(reward)>std::numeric_limits<std::uint32_t>::max()||std::floor(reward)!=reward)
                    return refuse(error,"career reward is not an integral currency value");
                m.reward[i]=static_cast<std::uint32_t>(reward);
            }
        }
        //49E420 kind2/kind3 messages are additional RewardPopup pages,
        //not extra cash. Kind4 exits without a message in49E676.
        std::vector<unsigned char> language;
        if(!load_race_image(root/"Languages/English.bin",language,issues) ||
           sha256_hex(sha256(language.data(),language.size()))!="6CBAB64235523149B95966B1844CDCA9FB858744126F8D9AD10E72C921AB8790")
            return refuse(error,"career reward language identity mismatch");
        const std::size_t count_strings=u32(language.data()+12),table_strings=8+u32(language.data()+16),strings=8+u32(language.data()+20);
        if(table_strings>language.size()||count_strings>(language.size()-table_strings)/8||strings>language.size())
            return refuse(error,"career reward language table bounds");
        const auto text=[&](std::uint32_t hash) {
            for(std::size_t i=0;i<count_strings;++i)if(u32(language.data()+table_strings+i*8)==hash) {
                const auto offset=u32(language.data()+table_strings+i*8+4);
                if(offset>=language.size()-strings)break;
                const auto start=strings+offset;auto end=start;
                while(end<language.size()&&language[end]&&end-start<512)++end;
                if(end==language.size()||language[end])break;
                return std::string(reinterpret_cast<const char*>(language.data()+start),end-start);
            }
            throw std::runtime_error("original reward message missing");
        };
        for(unsigned hash:{0xd14b7fcdU,0x817e429aU,0x8d9a96d8U,0x40ed9413U,0xdda5e29eU,
            0x7fb43183U,0x6480e3c3U,0xeaaffd6fU,0x836a10f0U,0xb86e03f0U,0xe39cfa9cU,
            0x74c345eeU,0x3f59e16eU,0xe855761aU,0x2ce41d89U,0x269a1649U,0xfcfe55f5U,
            0x49250a46U,0x0e67ada1U,0x994ac061U,0xfdc2f7d9U,0x35d89f9cU,0x78eff98fU,0x32429d3dU})
            next.info_labels_[hash]=text(hash);
        // Native 49E400 / 6FBA08: event cover text in magazine-key order.
        constexpr unsigned event_cover_messages[]={0x50e70065U,0x98f01a84U,0x71372a28U,
            0x0d74943fU,0xe93bb138U,0xe90c59d7U,0x024609f5U,0x98f01a84U,
            0xe93bb138U,0x54ef0621U,0x55b8d817U};
        // Native 49EBF0 / 6FBA60: task messages, ordered by cover index.
        constexpr unsigned task_cover_messages[]={0x08b85c11U,0x08b85c12U,0x08b85c13U,
            0x08b85c14U,0x08b85c15U,0x08b85c16U,0x08b85c17U,0x08b85c18U,
            0x08b85c19U,0x1fc3de61U,0x1fc3de62U,0x1fc3de63U,
            0x04052b3bU,0x1fc3de65U,0x1fc3de66U,0x1fc3de67U};
        for(unsigned i=0;i<next.magazine_task_messages_.size();++i)
            next.magazine_task_messages_[i]=text(task_cover_messages[i]);
        for(const auto& record:next.events_.records) {
            const auto event=record.number;
            if(!event||event>kCareerLastEvent)continue;
            const auto* entry=&record;
            const auto* raw=image.data()+entry->offset;const auto count=u32(raw+0x74);
            if(count>5)return refuse(error,"campaign reward count exceeds original slots");
            for(unsigned i=0;i<count;++i) {
                const auto kind=u32(raw+0x38+12*i),value=u32(raw+0x3c+12*i),direction=u32(raw+0x40+12*i);
                std::string key;unsigned wrapper=0x32f16f00U,content=0;
                if(kind==0) {
                    //49E43F formats the actual category name and level, then
                    //chooses its visual/performance wrapper. Level4 is claimed
                    //by the separate unique selector, not every normal-shop item.
                    constexpr const char* categories[]={"PAINT","DECALS","MUFFLER_TIPS","RIMS","WINDOW_TINT",
                        "SIDE_SKIRT","HEAD_LIGHTS","VINYLS","SPOILERS","FRONT_VALANCE","TAIL_LIGHTS",
                        "REAR_VALANCE","HOODS","ROOF_SCOOP","NEON","WIDE_BODY","PERF_WEIGHT_REDUCTION",
                        "PERF_BRAKES","PERF_ENGINE_EXHAUST","PERF_TRANSMISSION","PERF_TIRES","PERF_SUSPENSION",
                        "PERF_TURBO","PERF_ECU","PERF_NOS"};
                    if(value>=25||direction==0||direction>4)return refuse(error,"reward upgrade category/level invalid");
                    auto& level=next.media_[event].new_part_levels[value];
                    if(!level)level=static_cast<unsigned char>(direction);
                    if(direction==4)continue;
                    key=std::string("EVENT_REWARD_")+categories[value]+"_LEVEL_"+std::to_string(direction);
                    wrapper=value>=16?0xa512f58cU:0x43eeb333U;
                }else if(kind==1) {
                    unsigned index=0;
                    while(index<std::size(event_cover_messages)&&CareerMagazineCatalog::key(index)!=value)++index;
                    if(index==std::size(event_cover_messages))return refuse(error,"unknown original event cover message");
                    wrapper=0x7357569fU;
                    content=event_cover_messages[index];
                }else if(kind==2) {
                    const auto car=std::find_if(cars.records.begin(),cars.records.end(),[&](const auto& c){return bin_hash(c.name)==value;});
                    if(car==cars.records.end())return refuse(error,"reward car outside original table");
                    //49E510..49E5A4: PC USA aliases the regional car names.
                    key="EVENT_REWARD_CAR_"+(car->index==15?std::string("HYUNDAI"):car->name)+
                        ((car->index==14||car->index==15)?"_USA":"");
                }else if(kind==3)key="EVENT_REWARD_TRACK_"+std::to_string(value)+(direction==0?"_FORWARD":direction==1?"_BACKWARD":"");
                else continue;
                auto message=text(wrapper);const auto at=message.find("%s");
                if(at==std::string::npos)throw std::runtime_error("reward message placeholder missing");
                message.replace(at,2,text(content?content:bin_hash(key)));std::replace(message.begin(),message.end(),'^','\n');
                next.media_[event].unlock_messages.push_back(std::move(message));
            }
        }
        if(!next.tuning_.load(image,global,next.events_,error))return false;
        RaceChunkPayload presets;
        if(!find_race_chunk(image.data(),image.size(),0x30220,presets,issues)||
           (presets.end-presets.begin)%0x408)return refuse(error,"bonus car preset table invalid");
        for(const auto& reward:next.tuning_.style_rewards()) {
            //004AC220 retains every kind3 descriptor in original table order.
            if(reward.kind==3) {
                CareerBonusCar bonus;bonus.points=reward.points;
                // STYLE_SPONSOR_CAR_n is the descriptor identity, not an
                // English language-table key. Reuse the authored award name.
                bonus.label=text(reward.message);
                if(bonus.label.compare(0,4,"the ")==0)bonus.label.erase(0,4);
                if(!bonus.label.empty()&&bonus.label.back()=='.')bonus.label.pop_back();
                bool found=false;
                for(auto at=presets.begin;at<presets.end;at+=0x408) {
                    const auto read_name=[&](unsigned offset) {
                        std::string name;
                        for(unsigned i=0;i<32;++i){const auto c=image[std::size_t(at)+offset+i];
                            if(!c)return name;
                            if(c<32||c>126)throw std::runtime_error("bonus preset name invalid");
                            name+=char(c);}
                        throw std::runtime_error("bonus preset name unterminated");
                    };
                    const auto preset=read_name(0x28);
                    if(bin_hash(preset)!=reward.key)continue;
                    if(found)return refuse(error,"duplicate bonus preset");
                    found=true;bonus.preset=preset;bonus.car=read_name(8);
                    if(!resolve_frontend_preset_parts(image,std::size_t(at),bonus.car,preset,
                        next.tuning_.parts(),bonus.parts,error))return false;
                    //004B26F0 leaves slot19 at0057D160's explicit null,
                    //not at an inherited stock hood-decal selection.
                    bonus.parts.records[19]=kNoCarPart;
                }
                if(!found)return refuse(error,"bonus preset unavailable");
                const auto type=std::find_if(cars.records.begin(),cars.records.end(),[&](const auto& c){return c.name==bonus.car;});
                if(type==cars.records.end())return refuse(error,"bonus car type unavailable");
                //0057D160 clears performance before004B26F0.004BC830 then
                //assigns level2 to the nine fields only for types3,6 and8.
                if(type->index==3||type->index==6||type->index==8)bonus.performance.fill(2);
                next.bonus_cars_.push_back(std::move(bonus));
            }
        }
        if(!next.magazines_.load(global,error))return false;
        for(const auto& reward:next.tuning_.style_rewards()) {
            auto message=text(0x32f16f00U);const auto at=message.find("%s");
            if(at==std::string::npos)throw std::runtime_error("style reward placeholder missing");
            message.replace(at,2,text(reward.message));std::replace(message.begin(),message.end(),'^','\n');
            next.style_messages_.push_back(std::move(message));
        }
        CareerRatingBounds bounds;
        for(const auto& car:cars.records) {
            const auto* record=global.data()+car.offset;
            if(u32(record+0xc54)==0)bounds.add(record+0x110);
        }
        next.rating_low_=bounds.low;next.rating_high_=bounds.high;
        for(const auto& car:cars.records)next.rating_records_.push_back({car.name,
            std::vector<unsigned char>(global.begin()+car.offset,global.begin()+car.offset+kVehicleRecordSize)});
        // Native4C3240/5A1630: enabled regional PC cars, with original type2
        // event rewards controlling availability independently of starter prices.
        for(const auto& name:playable_car_names(cars)) {
            const auto car=std::find_if(cars.records.begin(),cars.records.end(),[&](const auto& v){return v.name==name;});
            CareerCar entry{name,car->brand_name,u32(global.data()+car->offset+0xc7c),{}};
            if(!career_stock_ratings(global.data()+car->offset,bounds,entry.performance))return refuse(error,"trade car ratings unavailable");
            next.cars_.push_back(std::move(entry));
            std::vector<unsigned> rewards;
            for(const auto& event:next.events_.records) {
                const auto* r=image.data()+event.offset;
                for(unsigned n=0;n<u32(r+0x74);++n)
                    if(u32(r+0x38+n*12)==2&&u32(r+0x3c+n*12)==bin_hash(name))rewards.push_back(event.number);
            }
            next.car_unlocks_.push_back({name,std::move(rewards)});
        }
        const auto* event=find_race_event(next.events_,0);if(!event)return refuse(error,"career intro missing");
        const auto* raw=image.data()+event->offset;const auto count=u32(raw+0x74);
        if(count>5)return refuse(error,"career intro reward list exceeds original slots");
        // Original choose-car list4C6D29 walks6FAD60 by16; its car indices,
        // filtered by event0's type2 unlock rewards. Prices are CarType+C7C.
        constexpr unsigned order[]={0,8,2,6,7,1,3,5,4};
        for(auto index:order) {
            if(index>=cars.records.size())return refuse(error,"starter index absent");
            const auto& car=cars.records[index];bool unlocked=false;
            for(unsigned i=0;i<count;++i)if(u32(raw+0x38+12*i)==2 &&
                u32(raw+0x3c+12*i)==bin_hash(car.name))unlocked=true;
            if(unlocked) {
                CareerCar entry{car.name,car.brand_name,u32(global.data()+car.offset+0xc7c),{}};
                if(!career_stock_ratings(global.data()+car.offset,bounds,entry.performance))
                    return refuse(error,"original starter performance unavailable");
                next.starters_.push_back(std::move(entry));
            }
        }
        if(next.starters_.empty())return refuse(error,"original starter unlocks unresolved");
        *this=std::move(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool CareerCatalog::performance_ratings(const CareerProfile& p,std::array<float,3>& out)const {
    const auto found=std::find_if(rating_records_.begin(),rating_records_.end(),[&](const auto& r){return r.first==p.car;});
    if(found==rating_records_.end())return false;
    const auto levels=career_performance_levels(*this,p);
    std::array<unsigned char,kVehicleRecordSize> applied{};std::vector<VehicleTuningIssue> issues;
    if(!apply_vehicle_tuning(found->second.data(),found->second.size(),levels,applied.data(),applied.size(),issues))return false;
    CareerRatingBounds bounds;bounds.low=rating_low_;bounds.high=rating_high_;
    return career_stock_ratings(applied.data(),bounds,out,levels.nitrous);
}
const char* quick_race_mode_name(QuickRaceMode mode) noexcept {
    constexpr const char* names[]={"CIRCUIT","SPRINT","DRAG","DRIFT","LAP KNOCKOUT","FREE RUN"};
    return names[unsigned(mode)];
}
std::vector<QuickRaceRow> quick_race_rows(QuickRaceMode mode,RaceType backing,bool bonus) {
    using Row=QuickRaceRow;
    std::vector<Row> rows{Row::track,Row::car};
    if(!bonus)rows.insert(rows.end(),{Row::customize,Row::performance,Row::unique});
    if(quick_race_type(mode,backing)!=RaceType::drift)rows.push_back(Row::traffic);
    if(mode!=QuickRaceMode::free_run) {
        rows.push_back(Row::difficulty);
        if(mode!=QuickRaceMode::drift) {
            rows.push_back(Row::opponents);
            rows.push_back(Row::catch_up);
        }
        if(mode==QuickRaceMode::circuit||mode==QuickRaceMode::drift)rows.push_back(Row::laps);
    }
    rows.push_back(Row::start);
    return rows;
}
std::vector<unsigned> CareerCatalog::quick_courses(QuickRaceMode mode)const {
    std::vector<unsigned> courses;
    for(unsigned i=0;i<free_courses_.size();++i) {
        const auto& course=free_courses_[i];
        if(mode==QuickRaceMode::free_run||(course.mode_mask&quick_race_track_mask(mode)))
            courses.push_back(i);
    }
    return courses;
}
bool CareerCatalog::load_free_drive(const std::filesystem::path& root,std::string& error) {
    CareerCatalog next;if(!next.load(root,error))return false;
    std::vector<unsigned char> global,language;std::vector<RaceTableIssue> issues;
    if(!load_race_image(root/"Global/GLOBALB.BUN",global,issues)||
       !load_race_image(root/"Languages/English.bin",language,issues))return refuse(error,"free driving catalog inputs unavailable");
    const auto types=parse_car_type_table(global.data(),global.size());
    const auto tracks=parse_track_info_table(global.data(),global.size());
    if(!types.ok()||!tracks.ok())return refuse(error,"quick race car or track table invalid");
    next.starters_.clear();CareerRatingBounds bounds;
    for(const auto& car:types.records)if(car.class_id==0)bounds.add(global.data()+car.offset+0x110);
    for(const auto& name:playable_car_names(types)) {
        CarAssets car;if(!resolve_car_assets(types,name,car,error))return false;
        CareerCar entry{name,car.brand_name,0,{}};
        if(!career_stock_ratings(global.data()+car.record_offset,bounds,entry.performance))return refuse(error,"free driving stock ratings unavailable");
        next.starters_.push_back(std::move(entry));
    }
    const auto label=[&](int track) {
        const auto key=bin_hash("TRACK_NAME_"+std::to_string(track));
        if(language.size()<24)return std::string{};
        const auto count=u32(language.data()+12),table=8+u32(language.data()+16),strings=8+u32(language.data()+20);
        if(table>language.size()||count>(language.size()-table)/8||strings>language.size())return std::string{};
        for(unsigned i=0;i<count;++i)if(u32(language.data()+table+i*8)==key) {
            const auto at=std::size_t(strings)+u32(language.data()+table+i*8+4);std::string result;
            for(auto n=at;n<language.size();++n){const auto c=language[n];if(!c)return result;if(c<32||c>126)return std::string{};result+=char(c);}
        }
        return std::string{};
    };
    for(const auto& e:next.events_.records)if(e.number>0&&e.number<=kCareerLastEvent) {
        for(unsigned round=0;round<e.races.size();++round) {
            SupportedRaceSelection selection;if(!supported_race_selection(e.number,round,selection))continue;
            const auto& slot=selection.slot;
            if(std::any_of(next.free_courses_.begin(),next.free_courses_.end(),[&](const auto& c){return c.track==slot.track&&c.direction==slot.direction;}))continue;
            auto name=label(slot.track);if(name.empty())return refuse(error,"free driving original track label absent");
            const auto* info=find_track_info(tracks,slot.track);
            if(!info)return refuse(error,"quick race track metadata absent");
            next.free_courses_.push_back({e.number,round,slot.track,slot.direction,selection.type,std::move(name),info->mode_mask});
        }
    }
    std::stable_sort(next.free_courses_.begin(),next.free_courses_.end(),[](const auto& a,const auto& b){return a.track!=b.track?a.track<b.track:a.direction<b.direction;});
    if(next.free_courses_.empty()||next.starters_.empty())return refuse(error,"free driving selection is empty");
    next.free_drive_=true;*this=std::move(next);error.clear();return true;
}
bool CareerCatalog::free_profile(const std::string& car,CareerProfile& p,std::string& error)const {
    if(!free_drive_||std::none_of(starters_.begin(),starters_.end(),[&](const auto& c){return c.name==car;}))return refuse(error,"free driving car unavailable");
    CareerProfile next;next.car=car;next.page=CareerPage::menu;next.customization_seen=true;
    next.style_high_water=0xffffffffU;
    for(const auto& e:events_.records)if(e.number<=kCareerLastEvent)next.status[e.number]=kEventStatusWon;
    if(!valid(next,error))return false;
    p=std::move(next);return true;
}
#include "career_bonus.inc"
const CareerEventMedia* CareerCatalog::media(unsigned event)const {
    return find_race_event(events_,event)?&media_[event]:nullptr;
}
std::vector<unsigned> CareerCatalog::available(const CareerProfile& p)const {
    std::vector<unsigned> result;const std::vector<std::uint8_t> status(p.status.begin(),p.status.end());
    for(const auto& e:events_.records)if(race_event_available(events_,e,status))result.push_back(e.number);
    return result;
}
bool CareerCatalog::select_available_event(CareerProfile& p)const {
    const auto events=available(p);
    if(events.empty() || std::find(events.begin(),events.end(),p.selected_event)!=events.end())return false;
    const auto next=std::lower_bound(events.begin(),events.end(),p.selected_event);
    p.selected_event=next==events.end()?events.back():*next;
    return true;
}
std::vector<std::string> CareerCatalog::style_messages(std::uint32_t before,std::uint32_t after)const {
    std::vector<std::string> result;const auto& rewards=tuning_.style_rewards();
    for(std::size_t i=0;i<rewards.size();++i)if(before<rewards[i].points&&rewards[i].points<=after)
        result.push_back(style_messages_.at(i));
    return result;
}
bool CareerCatalog::valid(const CareerProfile& p,std::string& error)const {
    if(!p.bonus_preset.empty()) {
        const auto* bonus=bonus_car(p.bonus_preset);
        if(!free_drive_||!bonus||bonus->car!=p.car)return refuse(error,"bonus car outside Quick Race");
    }
    if(!events_.ok()||p.difficulty>2||unsigned(p.page)>10||!find_race_event(events_,p.selected_event))return refuse(error,"invalid career selection");
    if(p.new_parts_event&&(p.new_parts_event>kCareerLastEvent||p.status[p.new_parts_event]!=kEventStatusWon))
        return refuse(error,"invalid career unlock context");
    const auto& statistics=p.statistics;
    if(!std::isfinite(statistics.metres)||statistics.metres<0||statistics.metres>1e15||
       statistics.wins>statistics.starts||statistics.losses>statistics.starts||statistics.totalled>statistics.starts)
        return refuse(error,"invalid career statistics");
    for(const auto& mode:statistics.modes)if(mode.wins>mode.starts||mode.starts>statistics.starts||
        mode.style<-INT64_C(1000000000000000)||mode.style>INT64_C(1000000000000000))return refuse(error,"invalid career mode statistics");
    if(p.magazine_pending&~0xffffU || p.magazine_covers.size()>27)return refuse(error,"invalid magazine state");
    if(p.page==CareerPage::magazines&&p.magazine_covers.empty())return refuse(error,"empty magazine viewer");
    std::array<bool,27> magazine_indices{};
    for(const auto& cover:p.magazine_covers){
        if(cover.index>=27||magazine_indices[cover.index]||cover.car.empty())return refuse(error,"invalid magazine snapshot");
        magazine_indices[cover.index]=true;
        if(std::none_of(cars_.begin(),cars_.end(),[&](const auto& car){return car.name==cover.car;}))return refuse(error,"unknown magazine car");
        for(unsigned slot=0;slot<120;++slot)if(cover.installed[slot]&&!tuning_.find_part(cover.car,slot,cover.installed[slot]))return refuse(error,"magazine part unavailable");
        for(auto level:cover.performance)if(level>3)return refuse(error,"magazine performance level invalid");
    }
    if(p.movie_phase>2 || (!p.movie_phase&&p.movie_event) ||
       (p.movie_phase && (!p.movie_event || !media(p.movie_event) ||
        (p.movie_phase==1?(p.page!=CareerPage::briefing || p.selected_event!=p.movie_event || media_[p.movie_event].before.empty()):
         (p.status[p.movie_event]!=kEventStatusWon || media_[p.movie_event].after.empty())))))
        return refuse(error,"invalid pending event movie");
    if(p.pending_unique) {
        if((p.pending_unique>kCareerLastEvent||media_[p.pending_unique].unique_categories.empty())||p.status[p.pending_unique]!=kEventStatusWon||
           p.page!=CareerPage::unique_reward)return refuse(error,"invalid pending unique award");
    }else if(p.page==CareerPage::unique_reward)return refuse(error,"unique selector has no earned award");
    const auto& tournament=p.tournament;
    if(tournament.event) {
        if(!supported_tournament_row(tournament.event)||tournament.event!=p.selected_event||
           tournament.round>supported_tournament_row(tournament.event)->count||p.status[tournament.event]==kEventStatusWon||p.car.empty())
            return refuse(error,"invalid active career tournament");
    }else if(tournament.round)return refuse(error,"inactive tournament has rounds");
    for(unsigned r=0;r<tournament.places.size();++r) {
        auto places=tournament.places[r];
        if(r<tournament.round) {
            std::sort(places.begin(),places.end());
            if(places!=std::array<unsigned,4>{{1,2,3,4}})return refuse(error,"invalid saved tournament places");
        }else if(places!=std::array<unsigned,4>{})return refuse(error,"unplayed tournament has a result");
    }
    for(unsigned i=0;i<p.status.size();++i)if(p.status[i]>5 || (p.status[i]&&!find_race_event(events_,i)))return refuse(error,"invalid career event status");
    if(!p.car.empty()&&!car_available(p,p.car))return refuse(error,"unknown or locked owned car");
    if(p.style_high_water<p.style_points)return refuse(error,"career style earned state below current points");
    bool customized=false;
    for(unsigned field=0;field<p.ride.performance.size();++field) {
        if(p.ride.unique[field]) {
            customized=true;
            if(!tuning_.category_available(unsigned(CareerTuningCatalog::performance_category(field)),4,p.status))
                return refuse(error,"career unique item not earned");
        }
        const auto level=p.ride.performance[field];
        if(level>3)return refuse(error,"career performance level invalid");
        if(level) {
            customized=true;
            if(!owns(p,unsigned(CareerTuningCatalog::performance_category(field)),level))return refuse(error,"career performance item not owned");
        }
    }
    for(unsigned category=0;category<p.ride.owned.size();++category) {
        bool gap=false;
        for(unsigned n=0;n<p.ride.owned[category].size();++n) {
            const auto key=p.ride.owned[category][n];
            if(!key){gap=true;continue;}
            customized=true;
            if(gap || std::find(p.ride.owned[category].begin(),p.ride.owned[category].begin()+n,key)!=p.ride.owned[category].begin()+n)
                return refuse(error,"career ownership list invalid");
            bool known=category>=16&&key<=3;
            if(category<16)for(const auto& part:tuning_.parts().records)
                if(CareerTuningCatalog::slot_category(part.slot)==int(category)&&tuning_.ownership_key(part)==key){known=true;break;}
            if(!known)return refuse(error,"career ownership key unknown");
        }
    }
    for(unsigned slot=0;slot<p.ride.installed.size();++slot)if(const auto hash=p.ride.installed[slot]) {
        customized=true;const auto* part=tuning_.find_part(p.car,slot,hash);
        if(!part)return refuse(error,"career installed part incompatible");
        const auto category=CareerTuningCatalog::slot_category(part->slot);
        const bool repaint=category==0&&paint_job_owned(p)&&tuning_.part_available(*part,p.status,p.style_high_water);
        if(category>=0&&part->upgrade_level&&!repaint&&!owns(p,unsigned(category),tuning_.ownership_key(*part)))return refuse(error,"career installed part not owned");
    }
    if(customized && p.car.empty())return refuse(error,"career customization lacks owned car");
    if(p.page==CareerPage::intro) {
        if(!p.car.empty()||p.bank||p.style_points||p.style_high_water||customized||p.customization_seen||std::any_of(p.status.begin(),p.status.end(),[](auto s){return s!=0;}))return refuse(error,"inconsistent new career");
    }else {
        if(p.status[0]!=kEventStatusWon)return refuse(error,"career continuation lacks UG0 win");
        const bool before_buy=p.page==CareerPage::intro_short||p.page==CareerPage::choose_car;
        if(before_buy!=p.car.empty())return refuse(error,"career ownership disagrees with transition");
        if((p.page==CareerPage::garage||p.page==CareerPage::customize_movie)&&!p.customization_seen)return refuse(error,"customization tutorial state missing");
    }
    error.clear();return true;
}
bool CareerCatalog::intro_won(CareerProfile& p,std::string& error)const {
    if(!valid(p,error))return false;
    if(p.page!=CareerPage::intro)return refuse(error,"UG0 reward already applied");
    p.bank=media_[0].reward[p.difficulty];p.status[0]=kEventStatusWon;p.page=CareerPage::intro_short;
    return true;
}
bool CareerCatalog::begin_event(CareerProfile& p,std::string& error)const {
    if(!valid(p,error))return false;
    const auto eligible=available(p);
    if(p.selected_event>kCareerLastEvent||std::find(eligible.begin(),eligible.end(),p.selected_event)==eligible.end())
        return refuse(error,"career event unavailable");
    if(supported_tournament_row(p.selected_event)) {
        if(p.tournament.event!=p.selected_event)p.tournament={p.selected_event,0,{}};
    }else p.tournament={};
    p.page=CareerPage::briefing;
    //4C4F00/4C4EE0 routes7/15/92 through NewCareerFMV before difficulty.
    //4C5DF0 handles other authored before movies after difficulty selection.
    //The same metadata supplies late before/after movies through the finale.
    if(!media_[p.selected_event].before.empty()) {p.movie_event=p.selected_event;p.movie_phase=1;}
    error.clear();return true;
}
//4CDDB0 selects the tournament warning while5A2F10 reports unfinished
//rounds. Confirmed leave discards that tournament's intermediate standings.
bool CareerCatalog::abandon_event(CareerProfile& p,std::string& error)const {
    if(!valid(p,error))return false;
    if(p.page!=CareerPage::briefing)return refuse(error,"no active career race to leave");
    auto next=p;next.tournament={};next.movie_event=next.movie_phase=0;
    if(next.races_lost<std::numeric_limits<std::uint32_t>::max())++next.races_lost;
    next.page=CareerPage::map;
    if(!valid(next,error))return false;
    p=std::move(next);error.clear();return true;
}
bool apply_career_race_result(const CareerCatalog& career,CareerProfile& next,unsigned place,
    const std::array<unsigned,4>& places,int style,std::string& error) {
    if(!place)return career.abandon_event(next,error);
    if(place==1)++next.races_won;else ++next.races_lost;
    if(next.tournament.event) {
        if(!career.tournament_round_finished(next,places,error))return false;
    }else if(!career.event_finished(next,next.selected_event,place,error))return false;
    next.style_points=static_cast<std::uint32_t>(std::clamp<std::int64_t>(
        std::int64_t(next.style_points)+style,0,0xffffffffLL));
    next.style_high_water=std::max(next.style_high_water,next.style_points);
    return true;
}
bool CareerCatalog::tournament_round_finished(CareerProfile& p,const std::array<unsigned,4>& places,std::string& error)const {
    if(!valid(p,error))return false;
    auto sorted=places;std::sort(sorted.begin(),sorted.end());
    if(p.page!=CareerPage::briefing||!p.tournament.event||p.tournament.round>=supported_tournament_row(p.tournament.event)->count||
       sorted!=std::array<unsigned,4>{{1,2,3,4}})return refuse(error,"invalid tournament round result");
    auto next=p;next.tournament.places[next.tournament.round++]=places;
    if(next.tournament.round==supported_tournament_row(next.tournament.event)->count) {
        const auto order=next.tournament.order();
        const auto place=unsigned(std::find(order.begin(),order.end(),0U)-order.begin())+1;
        if(!event_finished(next,next.selected_event,place,error))return false;
    }
    p=std::move(next);error.clear();return true;
}
bool CareerCatalog::event_finished(CareerProfile& p,unsigned event,unsigned place,std::string& error)const {
    if(!valid(p,error))return false;
    if((event<1 || event>kCareerLastEvent) || p.selected_event!=event || p.page!=CareerPage::briefing || place<1 || place>4)
        return refuse(error,"unsupported career event result");
    if(supported_tournament_row(event)&&(!p.tournament.event||p.tournament.round!=supported_tournament_row(event)->count))
        return refuse(error,"tournament requires every round");
    const auto eligible=available(p);
    if(std::find(eligible.begin(),eligible.end(),event)==eligible.end())return refuse(error,"career event unavailable");
    if(place==1) {
        const auto reward=media_[event].reward[p.difficulty];
        if(reward>std::numeric_limits<std::uint32_t>::max()-p.bank)return refuse(error,"career reward overflow");
        // Original5A0DC0 updates the saved reward context on a win.
        p.bank+=reward;p.status[event]=kEventStatusWon;p.new_parts_event=event;
        for(auto key:media_[event].magazine_rewards){
            unsigned index=0;while(index<27&&CareerMagazineCatalog::key(index)!=key)++index;
            if(index==27)return refuse(error,"event magazine identity unavailable");
            if(std::none_of(p.magazine_covers.begin(),p.magazine_covers.end(),[&](const auto& cover){return cover.index==index;})){
                CareerMagazine cover;cover.index=index;cover.seed=p.magazine_random;cover.car=p.car;cover.installed=p.ride.installed;
                cover.performance=p.ride.performance;cover.unique=p.ride.unique;CareerMagazineRecipe recipe;
                if(!magazines_.make(key,p.magazine_random,recipe,error))return false;
                p.magazine_covers.push_back(std::move(cover));
            }
        }
    }
    if(place!=1&&(find_race_event(events_,event)->kind==2||find_race_event(events_,event)->kind==3))p.status[event]=5;
    p.movie_event=p.movie_phase=0;
    if(place==1&&!media_[event].after.empty()){p.movie_event=event;p.movie_phase=2;}
    p.tournament={};p.page=CareerPage::menu;
    select_available_event(p);
    if(place==1&&!media_[event].unique_categories.empty()) {
        p.pending_unique=event;p.page=CareerPage::unique_reward;
    }
    if(place==1&&!refresh_magazines(p,true,error))return false;
    if(place==1&&p.page==CareerPage::menu&&std::any_of(p.magazine_covers.begin(),p.magazine_covers.end(),[](const auto& c){return !c.seen;}))p.page=CareerPage::magazines;
    error.clear();return true;
}
bool CareerCatalog::award_unique(CareerProfile& p,unsigned category,std::uint32_t hash,std::string& error)const {
    if(!valid(p,error))return false;
    if(!p.pending_unique||p.page!=CareerPage::unique_reward)return refuse(error,"unique award unavailable");
    const auto& choices=media_[p.pending_unique].unique_categories;
    if(std::find(choices.begin(),choices.end(),category)==choices.end())return refuse(error,"unique award category not offered");
    auto next=p;
    if(category>=16) {
        unsigned field=0;while(field<9&&unsigned(CareerTuningCatalog::performance_category(field))!=category)++field;
        if(field==9||hash)return refuse(error,"unique performance mapping unavailable");
        if(next.ride.unique[field])return refuse(error,"unique performance already installed");
        next.ride.unique[field]=true;
    }else {
        //4C8720/5A2620 grants one selected visual item for its actual slot.
        const auto slot=category==12?8U:category==3?17U:category==7?31U:category==8?11U:category==15?6U:120U;
        const auto* part=tuning_.find_part(p.car,slot,hash);
        if(!part||part->upgrade_level!=4||CareerTuningCatalog::slot_category(part->slot)!=int(category))
            return refuse(error,"unique visual part incompatible");
        auto& inventory=next.ride.owned[category];const auto key=tuning_.ownership_key(*part);
        if(!owns(next,category,key)) {
            const auto free=std::find(inventory.begin(),inventory.end(),0U);
            if(free==inventory.end())return refuse(error,"unique inventory full");
            *free=key;
        }
        next.ride.installed[slot]=hash;
    }
    next.pending_unique=0;next.page=CareerPage::menu;
    if(std::any_of(next.magazine_covers.begin(),next.magazine_covers.end(),[](const auto& cover){return !cover.seen;}))next.page=CareerPage::magazines;
    if(!valid(next,error))return false;
    p=std::move(next);error.clear();return true;
}
bool CareerCatalog::buy(CareerProfile& p,std::size_t index,std::string& error)const {
    if(!valid(p,error))return false;
    if(p.page!=CareerPage::choose_car||index>=starters_.size())return refuse(error,"starter purchase unavailable");
    const auto& c=starters_[index];if(c.cost>p.bank)return refuse(error,"insufficient career funds");
    p.car=c.name;p.bank-=c.cost;p.page=CareerPage::intro_long;return true;
}
#include "career_trade.inc"
bool CareerCatalog::owns(const CareerProfile& p,unsigned category,std::uint32_t key)const noexcept {
    return (free_drive_&&category<p.ride.owned.size()&&key) || (category<p.ride.owned.size() && key &&
        std::find(p.ride.owned[category].begin(),p.ride.owned[category].end(),key)!=p.ride.owned[category].end());
}
std::uint32_t CareerCatalog::part_price(const CareerProfile& p,const CarPartRecord& part)const noexcept {
    if(free_drive_)return 0;
    const auto category=CareerTuningCatalog::slot_category(part.slot);
    if(!part.upgrade_level||(category>=0&&owns(p,unsigned(category),tuning_.ownership_key(part)))||
       (category==0&&paint_job_owned(p)))return 0;
    return tuning_.part_price(part);
}
bool CareerCatalog::buy_part(CareerProfile& p,unsigned target_slot,std::uint32_t part_hash,std::string& error)const {
    if(!valid(p,error))return false;
    if(p.page!=CareerPage::garage || target_slot>=p.ride.installed.size())return refuse(error,"part purchase requires garage and valid slot");
    auto next=p;
    if(!part_hash) {
        // Restore the original stock/empty slot; keep purchased inventory.
        next.ride.installed[target_slot]=0;
    }else {
        const auto* part=tuning_.find_part(p.car,target_slot,part_hash);
        if(!part)return refuse(error,"selected part incompatible with owned car");
        if(target_slot>=37&&target_slot<=41) {
            constexpr unsigned physical[]={12,11,16,15,17};const auto slot=physical[target_slot-37];
            const auto* component=tuning_.find_part(p.car,slot,p.ride.installed[slot]);
            const bool mechanical=(target_slot==37&&p.ride.performance[2]>0)||(target_slot==39&&p.ride.performance[8]>0);
            if((!component||!component->upgrade_level)&&!mechanical)return refuse(error,"You cannot paint stock parts.");
        }
        if(target_slot>=19&&target_slot<=25) {
            // Decal geometry is a layout selector, not an independent paid
            // upgrade. Sponsor selection is committed in the same transaction.
            next.ride.installed[target_slot]=part_hash;
            if(!valid(next,error))return false;
            p=std::move(next);error.clear();return true;
        }
        const auto category=unsigned(CareerTuningCatalog::slot_category(part->slot));
        const auto key=tuning_.ownership_key(*part);
        if(part->slot==58&&!tuning_.part_available(*part,p.status,p.style_high_water))return refuse(error,"selected sponsor is locked");
        const bool owned=part->upgrade_level==0 || owns(p,category,key);
        if(!owned) {
            if(!tuning_.part_available(*part,p.status,p.style_high_water))return refuse(error,"selected part is locked");
            if(!key)return refuse(error,"selected part ownership identity unavailable");
            const auto price=part_price(p,*part);
            if(price>p.bank)return refuse(error,"insufficient career funds");
            //Color changes consume the existing paint job, not another slot
            //in the36-entry purchased-parts inventory. Locked palettes still
            //failed part_available above, even when repainting is free.
            if(category!=0||!paint_job_owned(p)) {
                auto& inventory=next.ride.owned[category];const auto free=std::find(inventory.begin(),inventory.end(),0U);
                if(free==inventory.end())return refuse(error,"career part inventory is full");
                *free=key;
            }
            next.bank-=price;
        }
        next.ride.installed[target_slot]=part_hash;
        // Original4E56B0 moves a design instead of duplicating it on layers.
        // The other layer's color slots are retained by the original handler.
        if(target_slot>=31&&target_slot<=34)
            for(unsigned layer=31;layer<=34;++layer)
                if(layer!=target_slot&&next.ride.installed[layer]==part_hash)next.ride.installed[layer]=0;
    }
    if(!valid(next,error))return false;
    if(!free_drive_&&!refresh_magazines(next,false,error))return false;
    p=std::move(next);error.clear();return true;
}
bool CareerCatalog::buy_performance(CareerProfile& p,unsigned field,unsigned level,std::string& error)const {
    if(!valid(p,error))return false;
    if(p.page!=CareerPage::garage || field>=p.ride.performance.size() || level>(free_drive_?4U:3U))
        return refuse(error,"performance purchase requires garage and valid level");
    auto next=p;
    if(free_drive_&&level==4) {next.ride.unique[field]=!next.ride.unique[field];p=std::move(next);error.clear();return true;}
    const auto category=unsigned(CareerTuningCatalog::performance_category(field));
    if(level && !owns(p,category,level)) {
        if(!tuning_.category_available(category,level,p.status))return refuse(error,"selected performance package is locked");
        const auto price=tuning_.price(category,level);
        if(price>p.bank)return refuse(error,"insufficient career funds");
        auto& inventory=next.ride.owned[category];const auto free=std::find(inventory.begin(),inventory.end(),0U);
        if(free==inventory.end())return refuse(error,"career performance inventory is full");
        *free=level;next.bank-=price;
    }
    next.ride.performance[field]=level;
    if(!free_drive_&&!refresh_magazines(next,false,error))return false;
    if(!valid(next,error))return false;
    p=std::move(next);error.clear();return true;
}
bool CareerCatalog::dismiss_magazines(CareerProfile& p,std::string& error)const {
    if(!valid(p,error))return false;
    if(p.page!=CareerPage::magazines)return refuse(error,"magazine dismissal requires pending presentation");
    //4C9240/911AB364 acknowledges the complete Giveth batch, not only the
    //currently highlighted cover. Otherwise the next win reopens old rewards.
    for(auto& cover:p.magazine_covers)cover.seen=true;
    p.page=CareerPage::menu;error.clear();return true;
}
CareerFinishRewards CareerCatalog::finish_rewards(const CareerProfile& p,bool event_won)const {
    CareerFinishRewards result;
    if(!event_won||free_drive_||p.page!=CareerPage::briefing)return result;
    const auto* event=media(p.selected_event);
    if(!event)return result;
    // Native 496240: terminal victory and unique or magazine reward.
    result.collect=!event->unique_categories.empty()||!event->magazine_rewards.empty();
    const auto pending=magazine_task_mask(p);
    for(unsigned task=0;task<magazine_task_messages_.size();++task) {
        if(!(pending&(1U<<task)))continue;
        const auto seen=std::any_of(p.magazine_covers.begin(),p.magazine_covers.end(),
            [&](const auto& cover){return cover.index==task+11&&cover.seen;});
        if(seen)continue;
        result.magazine_tasks.push_back(magazine_task_messages_[task]);
        // The last four (online reputation) are acknowledged in the popup.
        if(task<12)result.collect=true;
    }
    return result;
}
std::uint32_t CareerCatalog::magazine_task_mask(const CareerProfile& p)const {
    auto pending=p.magazine_pending;
    if(p.car.empty())return pending;
    //51AED0: complete packages, including suspension, excluding aero fields.
    for(unsigned level=1;level<=3;++level)if(std::all_of(p.ride.performance.begin(),p.ride.performance.end(),[&](auto value){return value>=level;}))pending|=1U<<(level-1);
    InstalledPartSelection visual;
    const auto level=[&](unsigned slot){const auto* part=tuning_.find_part(p.car,slot,p.ride.installed[slot]);return part?unsigned(part->upgrade_level):0U;};
    for(unsigned slot=0;slot<120;++slot)if(const auto* part=tuning_.find_part(p.car,slot,p.ride.installed[slot]))visual.records[slot]=part->index;
    if(tuning_.reputation(visual)>=1.F)pending|=1U<<3;
    //51B060 uses actual installed levels and separate whole-body-kit override.
    unsigned front=level(1),rear=level(2),spoiler=level(10);if(level(6)==4)front=rear=spoiler=4;
    const unsigned hood=level(8),roof=level(11),exhaust=level(17),rims=level(15),neon=level(7),lights=level(115),tails=level(116),mirror=level(14),paint=level(30),side=level(13);
    unsigned vinyl=0;for(unsigned slot=31;slot<=34;++slot)vinyl=std::max(vinyl,level(slot));
    unsigned decal=0;
    for(unsigned slot=59;slot<=66;++slot)if(p.ride.installed[slot])decal=3;
    for(unsigned slot=83;slot<=114&&!decal;++slot)if(p.ride.installed[slot])decal=2;
    for(unsigned slot=67;slot<=82&&!decal;++slot)if(p.ride.installed[slot])decal=1;
    if(front&&rear&&spoiler&&roof&&hood&&exhaust&&decal&&vinyl&&lights&&tails)pending|=1U<<4;
    if(front>1&&rear>1&&spoiler>1&&roof>1&&hood>1&&exhaust>1&&decal>1&&vinyl>1&&neon>1&&lights>1&&tails>1&&rims>1&&paint>1)pending|=1U<<5;
    if(front>2&&rear>2&&spoiler>2&&roof>2&&side>2&&mirror>2&&exhaust>2&&(decal>2||hood>2)&&vinyl>2&&neon>2&&lights>2&&tails>2&&rims>2&&paint>2)pending|=1U<<6;
    if(hood>3&&exhaust>3&&roof>3&&vinyl>3&&front>2&&rear>2&&spoiler>2&&side==3&&mirror==3&&neon==3&&lights==3&&tails==3&&rims==3&&paint==3)pending|=1U<<7;
    return pending;
}
bool CareerCatalog::refresh_magazines(CareerProfile& p,bool capture,std::string& error)const {
    if(p.car.empty()){error.clear();return true;}
    p.magazine_pending=magazine_task_mask(p);
    if(capture)for(unsigned task=0;task<16;++task)if(p.magazine_pending&(1U<<task)){
        const auto index=task+11;
        if(std::any_of(p.magazine_covers.begin(),p.magazine_covers.end(),[&](const auto& cover){return cover.index==index;}))continue;
        CareerMagazine cover;cover.index=index;cover.seed=p.magazine_random;cover.car=p.car;cover.installed=p.ride.installed;
        cover.performance=p.ride.performance;cover.unique=p.ride.unique;CareerMagazineRecipe recipe;
        if(!magazines_.make(CareerMagazineCatalog::key(index),p.magazine_random,recipe,error))return false;
        p.magazine_covers.push_back(std::move(cover));
    }
    error.clear();return true;
}
std::string CareerCatalog::pending_movie(const CareerProfile& p,bool race_confirmed)const {
    if(p.page!=CareerPage::unique_reward&&p.movie_phase&&p.movie_event<media_.size()) {
        if(p.movie_phase==1&&!race_confirmed&&p.movie_event!=7&&p.movie_event!=15&&p.movie_event!=92)return {};
        const auto& media=media_[p.movie_event];
        return (p.movie_phase==1?media.before:media.after)+".mad";
    }
    if(p.page==CareerPage::intro_short)return media_[0].after+"_s.mad";
    if(p.page==CareerPage::intro_long)return media_[0].after+"_e.mad";
    if(p.page==CareerPage::customize_movie)return "04_GUM_s.mad";
    return {};
}
bool CareerCatalog::movie_finished(CareerProfile& p,std::string& error)const {
    if(!valid(p,error))return false;
    if(p.movie_phase){p.movie_event=p.movie_phase=0;error.clear();return true;}
    switch(p.page) {
    case CareerPage::intro_short:p.page=CareerPage::choose_car;break;
    case CareerPage::intro_long:p.page=CareerPage::map;break;
    case CareerPage::customize_movie:p.page=CareerPage::garage;break;
    default:return refuse(error,"no career movie is pending");
    }
    return true;
}
bool CareerCatalog::enter_customization(CareerProfile& p,std::string& error)const {
    if(!valid(p,error))return false;
    if(p.page!=CareerPage::menu)return refuse(error,"customization requires Underground menu");
    p.page=p.customization_seen?CareerPage::garage:CareerPage::customize_movie;p.customization_seen=true;return true;
}
}
