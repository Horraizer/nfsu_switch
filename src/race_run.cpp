#include "nfsu/race_run.h"
#include "nfsu/start_crowd.h"
#include "nfsu/car_assets.h"
#include "nfsu/car_collision_hull.h"
#include "nfsu/race_events.h"
#include "nfsu/platform.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <fstream>
#include <functional>
#include <cstring>
#include <cstdio>

namespace nfsu {
namespace {
std::filesystem::path asset(const std::filesystem::path& root, const TrackSlice& track, const char* name) {
    for (const auto& file : track.assets.files)
        if (file.status == AssetStatus::ok && asset_names_match(file.expected.path, name)) return root/file.found_as;
    return {};
}
}
//47B890 binds34035 directly.47F0AC derives the countdown from authored
// frames; DDay2338 has no cinematic camera (47C020:+60 and+6C both zero).
// Header-only traversal avoids another resident copy of STREAML1RA.
bool load_race_start_timing(const std::filesystem::path& root,unsigned event,float& value,float& cinematic,std::string& error,unsigned race_index) {
    SupportedRaceSelection selection;
    if(!supported_race_selection(event,race_index,selection)){error="start timing selection unavailable";return false;}
    const bool drift=selection.type==RaceType::drift;
    std::ifstream file(root/(std::string("Tracks/STREAM")+(drift?selection.region:"L1RA")+".BUN"),std::ios::binary|std::ios::ate);
    if(!file){error="intro scene stream unavailable";return false;}
    const auto length=file.tellg();if(length<8){error="intro scene stream empty";return false;}
    auto word=[](const unsigned char* p){return std::uint32_t(p[0])|std::uint32_t(p[1])<<8|std::uint32_t(p[2])<<16|std::uint32_t(p[3])<<24;};
    unsigned visits=0;bool found=false;
    std::function<bool(std::uint64_t,std::uint64_t,unsigned)> walk;
    walk=[&](std::uint64_t at,std::uint64_t end,unsigned depth) {
        if(depth>16)return false;
        while(at<end) {
            if(++visits>200000 || end-at<8)return false;
            unsigned char h[8];file.seekg(at);if(!file.read(reinterpret_cast<char*>(h),8))return false;
            const auto kind=word(h),size=word(h+4);const auto first=at+8;
            if(size>end-first)return false;
            if(kind==0x34035 && size>=0x70) {
                unsigned char scene[0x70];if(!file.read(reinterpret_cast<char*>(scene),sizeof scene))return false;
                if(word(scene+4)==(drift?kFirstDriftStartScene:2338U)) {
                    if(found)return false;
                    const auto stop=word(scene+0x64),go=word(scene+0x68),release=word(scene+0x6c);
                    if(stop<go || go<=release || word(scene+0x60)!=0 || (!drift && release!=0))return false;
                    // Native x87 keeps the intermediate before one final float store.
                    value=float((double(go)*(1.0/double(30.0F))-double(release)*(1.0/double(30.0F)))*double(1.0F/3.0F));
                    cinematic=float(double(release)/30);found=true;
                }
            }else if((kind&0x80000000U) && !walk(first,first+size,depth+1))return false;
            at=first+size;
        }
        return true;
    };
    if(!walk(0,std::uint64_t(length),0) || !found){error="intro scene timing or camera domain unsupported";return false;}
    return true;
}

bool RaceRun::set_event(std::uint32_t event,std::string& error,unsigned race_index) {
    SupportedRaceSelection selection;
    if(metadata_ready_||session_||quick_race_||!supported_race_selection(event,race_index,selection)) {
        error="event and round must select a supported race before metadata/start";return false;
    }
    event_number_=event;race_index_=race_index;race_type_=selection.type;
    track_number_=std::uint32_t(selection.slot.track);return true;
}
bool RaceRun::set_start_clock(unsigned release,unsigned go,std::string& error) {
    if(session_ || !release || go<=release || go>10000 || free_drive_ || race_type_==RaceType::drag || race_type_==RaceType::drift) {
        error="authored start clock domain or session already started";return false;
    }
    authored_cinematic_=float(double(release)/30);
    authored_countdown_=float((double(go)/30-double(release)/30)*double(1.F/3.F));
    authored_start_=true;error.clear();return true;
}
bool RaceRun::set_crash_camera(unsigned intensity,std::string& error) {
    if(metadata_ready_||session_||intensity>255){error="crash camera must be configured before metadata/start";return false;}
    crash_intensity_=intensity;error.clear();return true;
}
bool RaceRun::set_difficulty(unsigned difficulty,std::string& error) {
    if(metadata_ready_||session_||quick_race_||difficulty>2){error="difficulty must be 0..2 before metadata/start";return false;}
    difficulty_=difficulty;return true;
}
bool RaceRun::set_performance_levels(const VehicleUpgradeLevels& levels, std::string& error) {
    if (metadata_ready_ || session_) { error = "performance setup must precede metadata/start"; return false; }
    // Validate levels and flags through the same application path before
    // committing configuration, without any retail file access.
    std::array<unsigned char,kVehicleRecordSize> original{};
    encode_vehicle_handling(stock_240sx_handling(),original.data(),original.size());
    encode_vehicle_tuning(stock_240sx_tuning(),original.data(),original.size());
    VehicleSpec checked;
    if (!make_race_vehicle_spec(original.data(),original.size(),levels,checked,error)) return false;
    levels_ = levels; explicit_performance_ = true; return true;
}
bool RaceRun::set_free_drive(bool enabled, std::string& error) {
    if (metadata_ready_ || session_ || quick_race_) { error = "driving mode must precede metadata/start"; return false; }
    free_drive_ = enabled;
    return true;
}
bool RaceRun::set_quick_race(const QuickRaceOptions& options,std::string& error) {
    if(metadata_ready_||session_||quick_race_||event_number_==0||unsigned(options.mode)>=kQuickRaceModeCount||
       options.traffic>3||options.opponents>3||options.difficulty>2||options.laps<1||options.laps>10||
       (options.mode==QuickRaceMode::lap_knockout&&options.opponents<1)) {
        error="invalid quick race options";return false;
    }
    quick_race_=options;
    free_drive_=options.mode==QuickRaceMode::free_run;
    race_type_=quick_race_type(options.mode,race_type_);
    difficulty_=options.difficulty;error.clear();return true;
}
bool RaceRun::set_steering(RaceSteering mode, std::string& error) {
    if (metadata_ready_ || session_) { error="steering must precede metadata/start"; return false; }
    if (mode != RaceSteering::baseline && mode != RaceSteering::recovered_predicate0 &&
        mode != RaceSteering::recovered_predicate1) { error="invalid steering mode"; return false; }
    steering_=mode; return true;
}
bool RaceRun::set_car(const std::string& car_type_name, std::string& error) {
    if (metadata_ready_ || session_) { error="car selection must precede metadata/start"; return false; }
    if (car_type_name.empty()) { error="car selection needs a car type name"; return false; }
    car_=car_type_name; return true;
}
bool RaceRun::set_intro_grid(const VehiclePose& pose,const VehicleHull& hull,
                            const std::vector<RaceRivalSetup>& rivals,const std::array<int,3>& contexts,std::string& error,
                            unsigned player_grid) {
    if(metadata_ready_||session_||race_type_==RaceType::drift||(race_type_==RaceType::time_trial?!rivals.empty():((rivals.empty()&&!quick_race_)||rivals.size()>3))||hull.points.empty()) {
        error="racing grid or collision hull invalid before metadata/start";return false;
    }
    if(player_grid==0||player_grid>rivals.size()+1){error="player grid outside the collision roster";return false;}
    player_grid_pose_=pose;player_hull_=hull;rivals_=rivals;rival_contexts_=contexts;player_grid_=player_grid;return true;
}
bool RaceRun::read_metadata(const std::filesystem::path& front, const std::filesystem::path& global,
                            std::string& error, TrackSliceLoadObserver* observer) {
    {
        SupportedRace race;
        if (!load_supported_race(front, global, event_number_, race, error,race_index_)) return false;
        if (observer) observer->checkpoint("race track info");
        std::vector<unsigned char> event_image;
        const auto events=load_race_event_table(front,event_image);const auto* descriptor=find_race_event(events,event_number_);
        if(!events.ok()||!descriptor||descriptor->offset+0x14c>event_image.size()){error="event AI settings unavailable";return false;}
        std::memcpy(&catchup_weight_,event_image.data()+descriptor->offset+0x12c,4);
        std::memcpy(&catchup_traffic_selector_,event_image.data()+descriptor->offset+0x134,4);
        if(catchup_traffic_selector_>3){error="catch-up traffic selector invalid";return false;}
        unsigned throttle_selector=0;
        std::memcpy(&throttle_selector,event_image.data()+descriptor->offset+0x148,4);
        if(!race_rival_throttle_cap(throttle_selector,rival_throttle_cap_)){error="event rival throttle selector invalid";return false;}
        if(quick_race_) {
            if(!free_drive_&&!(race.track.mode_mask&quick_race_track_mask(quick_race_->mode))) {
                error="track does not support the selected quick race mode";return false;
            }
            catchup_traffic_selector_=race_type_==RaceType::drift?0:quick_race_->traffic;
            catchup_weight_=quick_race_->catch_up&&!free_drive_?1.F:0.F;
            rival_throttle_cap_=1.F;
        }
        if(!free_drive_&&race_type_==RaceType::time_trial) {
            float seconds=0;std::memcpy(&seconds,event_image.data()+descriptor->offset+0x1c+4*difficulty_,4);
            if(!std::isfinite(seconds)||seconds<=0||double(seconds)*4000+.5>kRaceTicksLimit) {
                error="invalid original challenge deadline";return false;
            }
            time_limit_ticks_=std::int32_t(double(seconds)*4000+.5);
        }
        laps_ = quick_race_ ? static_cast<std::int8_t>(quick_race_laps(*quick_race_)) :
            free_drive_ ? 0 : race.slot.laps;
        direction_ = race.slot.direction;
        lap_skip_ = free_drive_ ? 0 : race.track.lap_skip;
    }
    if (observer) observer->checkpoint("race metadata released");
    metadata_ready_ = true;
    return true;
}
bool RaceRun::prepare_metadata(const std::filesystem::path& root, std::string& error,
                               TrackSliceLoadObserver* observer) {
    if (metadata_ready_ || session_) { error = "race metadata already prepared"; return false; }
    return read_metadata(root / "Frontend/FRONTB.LZC", root / "Global/GLOBALB.BUN", error, observer);
}
bool RaceRun::start(const std::filesystem::path& root, const TrackSlice& track, std::string& error,
                    TrackSliceLoadObserver* observer) {
    if (session_ || track.assets.verdict != AssetVerdict::complete) {
        error = "race requires a fresh session and a complete first-lap asset check"; return false;
    }
    if (!metadata_ready_ && !read_metadata(asset(root, track, "Frontend/FRONTB.LZC"),
                                             asset(root, track, "Global/GLOBALB.BUN"), error, observer)) return false;
    if (track.event_number != event_number_ || track.track_number != track_number_ || track.race_index != race_index_) {
        error = "race and uploaded track selections differ"; return false;
    }
    const auto trackb = asset(root, track, ("Tracks/" + track_bundle_file_name(static_cast<std::int16_t>(track_number_))).c_str());
    if (!load_vehicle_track_course(trackb, laps_, direction_, lap_skip_, 0, course_, error)) return false;
    if (observer) observer->checkpoint("race course prepared");
    {
        std::vector<unsigned char> bytes;
        zones_ = load_track_zones(trackb, bytes);
        table_ = parse_track_stream_table(bytes.data(), bytes.size());
    }
    if (observer) observer->checkpoint("race zones prepared");
    TrackStreamSelection selected;
    if (!select_track_stream_sections(table_, zones_, track_number_, TrackStreamSet::permanent, selected, error)) return false;
    std::set<std::size_t> startup_records(selected.records.begin(),selected.records.end());
    std::uint64_t startup_bytes=0;for(auto index:selected.records)startup_bytes+=table_.records[index].size;
    if(track.backing_set!=TrackStreamSet::permanent&&track.backing_set!=TrackStreamSet::zones) {
        error="race backing policy unavailable";return false;
    }
    if(track.backing_set!=TrackStreamSet::permanent&&
       !select_track_stream_sections(table_,zones_,track_number_,track.backing_set,selected,error))return false;
    prepared_.insert(selected.records.begin(),selected.records.end());
    std::uint64_t prepared_bytes=0;for(auto index:selected.records)prepared_bytes+=table_.records[index].size;
    if (prepared_.size() != track.sections) { error = "prepared track and race residency differ"; return false; }
    if(prepared_bytes!=track.input_bytes){error="prepared track byte coverage differs";return false;}
    TrackResidencyOptions options; options.budget_bytes = track_stream_pool_bytes(table_);
    std::vector<TrackResidencyStep> startup;
    if (!residency_.start(table_, zones_, track_number_, track_residency_table_costs(table_), options, startup, error)) return false;
    if (observer) observer->checkpoint("race residency prepared");
    const std::string version = "NFSU_RACE_STATE 1\n";
    sequence_.update(version.data(), version.size());
    for (const auto& step : startup) {
        if (!apply(step, error)) return false;
        const auto line = track_residency_step_line(table_, step) + "\n";
        sequence_.update(line.data(), line.size());
    }
    if (resident_ != startup_records || bytes_read_ != startup_bytes) {
        error = "race startup differs from the uploaded permanent set"; return false;
    }
    // Default and explicit zero levels use the same original runtime mass
    // writer. Serialized stock inertia differs by a binary32 rounding step;
    // retaining the built-in record here gives two different stock cars.
    VehicleSpec spec;
    if (!load_race_vehicle_spec(asset(root,track,"Global/GLOBALB.BUN"),car_,levels_,
                               spec,error,race_type_==RaceType::drift,difficulty_)) return false;
    if(event_number_==0 && rivals_.size()!=3){error="event0 needs its four-car grid";return false;}
    VehicleHull hull;
    if(!player_hull_.points.empty())hull=player_hull_;
    else if(car_=="240SX")hull=stock_240sx_hull();
    else {
        // Free driving and other selected-car events need that car's own
        // collision solid, exactly as the intro grid does. Load only at start;
        // discard the geometry image before constructing the live session.
        CarAssets selected;std::vector<unsigned char> geometry;
        std::vector<RaceTableIssue> issues;
        if(!load_car_assets(asset(root,track,"Global/GLOBALB.BUN"),car_,selected,error))return false;
        if(!load_race_image(root/selected.geometry_path,geometry,issues) ||
           !decode_car_collision_hull(geometry,bin_hash(car_+"_CV"),hull)) {
            error="selected car collision solid unavailable: "+car_;return false;
        }
    }
    if(!player_hull_.points.empty())course_.start=player_grid_pose_;
    if(race_type_==RaceType::drag)course_.start.gearbox=VehicleGearbox::manual;
    session_ = std::make_unique<RaceSession>(course_,spec,race_type_==RaceType::drift?RaceSteering::recovered_predicate1:steering_,hull);
    // 47D680 resolves anchors on the selected CarTypeInfo, never a global
    // camera table belonging to a different model. Read one bounded record.
    CarAssets camera_car;
    const auto global=asset(root,track,"Global/GLOBALB.BUN");
    if(!load_car_assets(global,car_,camera_car,error))return false;
    std::array<unsigned char,kVehicleRecordSize> camera_record{};
    std::ifstream camera_file(global,std::ios::binary);
    camera_file.seekg(static_cast<std::streamoff>(camera_car.record_offset));
    camera_file.read(reinterpret_cast<char*>(camera_record.data()),camera_record.size());
    ChaseCameraTable camera_table;
    if(!camera_file || !decode_chase_camera_table(camera_record.data(),camera_record.size(),camera_table,error) ||
       !session_->set_camera_table(camera_table,error)) {
        if(error.empty())error="selected camera record unavailable";
        return false;
    }
    if(!free_drive_&&time_limit_ticks_&&!session_->set_time_trial(time_limit_ticks_,error))return false;
    if(race_type_==RaceType::drag) {
        constexpr float quick_drag_shift_scale[]={3.F,1.5F,1.F};
        if(!session_->set_drag(true,error,quick_race_?quick_drag_shift_scale[difficulty_]:1.F)||
           !drag_controls_.configure(default_drag_controls_config(),error))return false;
    }
    if(race_type_==RaceType::lap_knockout && !rivals_.empty() &&
       !session_->set_lap_knockout(true,error,quick_race_?unsigned(rivals_.size()):3U))return false;
    if(race_type_==RaceType::drift&&!session_->prepare_style(root,error,event_number_))return false;
    if(!rivals_.empty()) {
        course_.start=player_grid_pose_;
        if(race_type_==RaceType::drag)course_.start.gearbox=VehicleGearbox::manual;
        std::array<float,3> low_slip{};
        std::array<const VehicleHull*,4> hulls{};hulls[3]=&player_hull_;
        for(unsigned i=0;i<rivals_.size();++i) {
            float rating=0;hulls[i]=&rivals_[i].hull;
            if(!ai_setup_performance(rivals_[i].spec.handling,rating,low_slip[i])) {
                error="intro selected performance refused";return false;
            }
        }
        for(unsigned i=0;i<rivals_.size();++i) {
            float rating=0,slip=0;ai_setup_performance(rivals_[i].spec.handling,rating,slip);
            std::printf("AI_DIFFICULTY_SETUP event=%u difficulty=%u slot=%u car=%s context=%d rating=%.9g low_slip=%.9g\n",
                unsigned(event_number_),difficulty_,i,rivals_[i].car_type.c_str(),rival_contexts_[i],rating,slip);
        }
        if(!intro_ai_.load(root,rival_contexts_,hulls,true,track_number_,direction_,race_type_==RaceType::drag,catchup_weight_,laps_,lap_skip_!=0,catchup_traffic_selector_,
            !quick_race_||quick_race_->catch_up)) {error="intro planner data refused";return false;}
        if(!session_->set_planned_rivals(rivals_,low_slip,error,player_grid_))return false;
        if(!session_->prepare_leaderboard(root,car_,error))return false;
        if(!session_->prepare_style(root,error,event_number_))return false;
    }
    if(rivals_.empty()&&(race_type_==RaceType::drag||race_type_==RaceType::drift)) {
        // Solo drag needs lane steering; drift uses the same human road
        // context only after finish (435310 controller3, action17).
        // No rival actors or difficulty contexts are created for this path.
        std::array<const VehicleHull*,4> hulls{};hulls[3]=&hull;
        if(!intro_ai_.load(root,rival_contexts_,hulls,true,track_number_,direction_,race_type_==RaceType::drag,catchup_weight_,laps_,false,catchup_traffic_selector_)) {
            error="solo finish/drag planner data refused";return false;
        }
    }
    // A live track world owns props even when its traffic selection is empty.
    // Solver-only replay runs keep their explicitly empty object world.
    if((free_drive_||traffic_configured_||!rivals_.empty()||race_type_==RaceType::time_trial||race_type_==RaceType::drift)&&
       !session_->set_traffic(root,traffic_,error,std::max(1U,catchup_traffic_selector_),intro_ai_.world_random()))return false;
    if((race_type_==RaceType::time_trial||
       (quick_race_&&!free_drive_&&race_type_!=RaceType::drift&&rivals_.empty()))&&
       !session_->prepare_style(root,error,event_number_))return false;
    if(crash_intensity_&&race_type_!=RaceType::drift&&race_type_!=RaceType::drag) {
        if(!crash_road_.load(root,track_number_,direction_,course_,spec.handling,rivals_.empty()?nullptr:&intro_ai_,error)||
           !session_->set_crash_camera(&crash_road_,crash_intensity_,track_number_,error))return false;
    }
    if (observer) observer->checkpoint("race session constructed");
    float countdown=kRaceCountdownStepSeconds,cinematic=0;
    if(!free_drive_&&(event_number_==0||race_type_==RaceType::drift) && !load_race_start_timing(root,event_number_,countdown,cinematic,error,race_index_))return false;
    if(authored_start_){countdown=authored_countdown_;cinematic=authored_cinematic_;}
    VehiclePose start;
    const bool drift_start=!free_drive_&&race_type_==RaceType::drift;
    if(drift_start) {
        SupportedRaceSelection selection;
        if(!supported_race_selection(event_number_,race_index_,selection)||
           !load_drift_start_pose(root,course_,start,error,selection.region))return false;
    }
    const auto started = session_->start(error,drift_start?&start:nullptr,countdown,cinematic);
    if (observer) observer->checkpoint("race session started");
    return started;
}
bool RaceRun::apply(const TrackResidencyStep& step, std::string& error) {
    for (auto r : step.releases) { resident_.erase(r); ++releases_; }
    for (auto r : step.loads) {
        if (!prepared_.count(r) || r >= table_.records.size()) {
            error = "race requested section "+std::to_string(r<table_.records.size()?table_.records[r].number:-1)+
                " outside uploaded backing for track "+std::to_string(track_number_); return false;
        }
        resident_.insert(r); ++loads_;
        bytes_read_ += table_.records[r].size;
    }
    return true;
}
bool RaceRun::set_intro_traffic(const std::vector<RaceTrafficSetup>& setup,std::string& error) {
    if(metadata_ready_||session_||(race_type_==RaceType::drift)||setup.size()>kRaceTrafficCapacity||(event_number_==0&&setup.size()!=6)) {error="intro traffic configuration invalid";return false;}
    traffic_=setup;traffic_configured_=true;error.clear();return true;
}
bool RaceRun::skip_start_cinematic() {
    return session_ && !pending_ && session_->skip_start_cinematic();
}
bool RaceRun::arm_finish_probe(std::string& error) {
    if(!session_||pending_){error="finish probe requires a committed live frame";return false;}
    return session_->arm_finish_probe(error);
}
bool RaceRun::simulate(const GamepadFrame& pad, std::string& error,
                       RaceSimulationProfile* profile,TimingService* timer) {
    if(profile && !timer){error="simulation profiling requires a clock";return false;}
    if(profile)*profile={};
    if (!session_ || pending_) { error = "race simulation/commit order violated"; return false; }
    if(session_->timing().outcome!=RaceOutcome::running&&
       !(session_->timing().outcome==RaceOutcome::finished&&(!rivals_.empty()||race_type_==RaceType::drift))) {error="race session is not running";return false;}
    if(!rivals_.empty()&&!intro_ai_.observe_human(*session_)){error="human difficulty history refused";return false;}
    int held_lane=0;
    if(race_type_==RaceType::drag) {
        drag_controls_.update(pad);const auto& f=drag_controls_.frame_state();
        const double steer=f.steer_from_button?f.steer_digital:f.steer_analog;
        // Port input binding: use the existing calibrated stick dead zone.
        held_lane=steer<0?-1:steer>0?1:0;
    }
    const bool drift_finish=race_type_==RaceType::drift&&session_->timing().outcome==RaceOutcome::finished;
    if((!rivals_.empty()||race_type_==RaceType::drag||drift_finish)&&(!race_car_held(session_->timing())||race_type_==RaceType::drag)) {
        RaceRivalPlans plans;
        const auto before=profile?timer->now_us():0;
        if(!intro_ai_.plan(*session_,plans,held_lane)) {error="intro path planner refused";return false;}
        // Retained driver+1FC also applies before GO. Traffic and the human
        // are separate actors and do not receive this racer constructor cap.
        for(std::size_t i=0;i<plans.count;++i) {
            plans.cars[i].throttle_cap=std::min(plans.cars[i].throttle_cap,rival_throttle_cap_);
            plans.cars[i].grid_throttle=std::min(plans.cars[i].grid_throttle,rival_throttle_cap_);
        }
        if(profile)profile->planner=(timer->now_us()-before)/1000.0;
        if(!session_->advance_planned(pad,plans,error,profile,timer))return false;
    } else if (!session_->advance(pad, error,profile,timer)) return false;
    pending_ = true; return true;
}
bool RaceRun::commit(std::string& error) {
    if (!session_ || !pending_) { error = "race commit without simulation"; return false; }
    const auto& car = session_->car();
    TrackResidencySample sample;
    sample.position[0] = float(car.position.x); sample.position[1] = float(car.position.y);
    sample.velocity[0] = float(car.velocity.x); sample.velocity[1] = float(car.velocity.y);
    TrackResidencyStep step;
    if (!residency_.advance(sample, step, error) || !apply(step, error)) return false;
    // Native residency can load/release sections within pre-uploaded backing.
    // Refuse any actual missing resource; do not fake a successful transition.
    if (!std::includes(prepared_.begin(),prepared_.end(),resident_.begin(),resident_.end())) {
        error = "race residency left the uploaded backing"; return false;
    }
    auto record = session_->record();
    const auto line = track_residency_step_line(table_, step) + "\n";
    record.insert(record.end(), line.begin(), line.end());
    for (auto index : resident_) {
        const auto r = static_cast<std::uint32_t>(index);
        for (unsigned k = 0; k < 4; ++k) record.push_back(static_cast<std::uint8_t>(r >> (8*k)));
    }
    const auto digest = sha256(record.data(), record.size());
    sequence_.update(digest.data(), digest.size());
    step_line_ = std::to_string(session_->timing().step) + " " + sha256_hex(digest) + "\n";
    steps_.update(step_line_.data(), step_line_.size());
    pending_ = false; return true;
}
std::string RaceRun::sequence_hash() const { auto copy = sequence_; return sha256_hex(copy.finish()); }
std::string RaceRun::steps_hash() const { auto copy = steps_; return sha256_hex(copy.finish()); }
bool RaceRun::matches_event1_replay() const {
    if (event_number_ != 1 || steering_ != RaceSteering::baseline) return false;
    if (!session_ || pending_) return false;
    // The pin is the 240SX player-only replay; no other car may claim it.
    if (car_ != "240SX") return false;
    if (explicit_performance_ && !performance_levels_equal(levels_,{})) return false;
    const auto& timing = session_->timing(); const auto& r = timing.racers[0];
    return timing.outcome == RaceOutcome::finished && timing.step == 48260 &&
        session_->telemetry().resets == 0 && r.highest_lap == 2 &&
        r.lap_ticks[1] == 1614080 && r.lap_ticks[2] == 1608400 && r.finish_ticks == 3222480 &&
        sequence_hash() == kEvent1SequenceHash && steps_hash() == kEvent1StepsHash;
}
bool RaceRun::matches_configured_replay() const {
    if (event_number_ != 1 || steering_ != RaceSteering::baseline) return false;
    // The performance replay is as 240SX as the baseline one.
    if (car_ != "240SX") return false;
    if (!explicit_performance_ || performance_levels_equal(levels_,{})) return matches_event1_replay();
    if (!session_ || pending_ || !performance_levels_equal(levels_,performance1_levels())) return false;
    const auto& timing=session_->timing(); const auto& r=timing.racers[0];
    return timing.outcome==RaceOutcome::finished && timing.step==48257 &&
        session_->telemetry().resets==0 && r.highest_lap==2 &&
        r.lap_ticks[1]==1613840 && r.lap_ticks[2]==1608440 && r.finish_ticks==3222280 &&
        sequence_hash()==kPerformance1SequenceHash && steps_hash()==kPerformance1StepsHash;
}
std::vector<std::string> RaceRun::result_lines() const {
    if (!session_) return {"RACE NOT STARTED"};
    const auto& t = session_->timing(); const auto& r = t.racers[0];
    std::vector<std::string> lines = {"event " + std::to_string(event_number_) + " track " + std::to_string(track_number_) + " laps " + std::to_string(r.highest_lap) +
        " outcome " + race_outcome_name(t.outcome) + " steps " + std::to_string(t.step) +
        " resets " + std::to_string(session_->telemetry().resets)};
    for (int i = 1; i <= r.highest_lap && i < kRaceLapSlots; ++i)
        lines.push_back("lap " + std::to_string(i) + " ticks " + std::to_string(r.lap_ticks[i]) +
                        " time " + format_race_ticks(r.lap_ticks[i]));
    lines.push_back("total ticks " + std::to_string(r.finish_ticks) + " time " + format_race_ticks(r.finish_ticks));
    if(session_->is_drift()) {
        lines.push_back("DRIFT RESULTS - position "+std::to_string(session_->player_position())+" / 4");
        for(const auto& row:session_->drift_competition().standings())
            lines.push_back(row.name+" "+std::to_string(int(row.score)));
    }
    if(session_->rival_count()) {
        lines.push_back("position "+std::to_string(session_->player_position())+" / 4");
        for(std::size_t i=0;i<session_->rival_count();++i) {
            const auto& rival=t.racers[i+1];
            lines.push_back(session_->rival_name(i)+" laps "+std::to_string(rival.highest_lap)+
                (rival.finish_valid?" finish "+format_race_ticks(rival.finish_ticks):" unfinished"));
        }
    }
    lines.push_back("state-sha256 " + sequence_hash());
    lines.push_back("steps-sha256 " + steps_hash());
    lines.push_back("stream loads " + std::to_string(loads_) + " releases " + std::to_string(releases_) +
                    " bytes-read " + std::to_string(bytes_read_));
    return lines;
}
bool RaceReplay::load(const std::string& text, std::string& error, bool performance1) {
    index_ = used_ = tick_ = 0; runs_.clear();
    if (sha256_hex(sha256(text.data(), text.size())) != (performance1 ? kPerformance1InputHash : kEvent1InputHash)) {
        error = "event1.txt does not match the pinned desktop recording"; return false;
    }
    return parse_race_input(text, runs_, error);
}
bool RaceReplay::next(std::uint64_t tick, GamepadFrame& pad, std::string& error) {
    if (tick != tick_ || exhausted()) { error = "replay exhausted or tick out of order"; return false; }
    pad = race_input_frame(runs_[index_], tick);
    if (++used_ == runs_[index_].ticks) { ++index_; used_ = 0; }
    ++tick_; return true;
}
void RaceTickDebt::add(std::uint64_t us) {
    const auto room = std::numeric_limits<std::uint64_t>::max() - units_;
    units_ += us > room/60 ? room : us*60;
}
bool RaceTickDebt::take() {
    if (units_ < 1000000) return false;
    units_ -= 1000000; return true;
}
} // namespace nfsu
