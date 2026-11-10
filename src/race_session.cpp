#include "nfsu/race_session.h"
#include "nfsu/platform.h"
#include "nfsu/ai_powertrain.h"
#include "nfsu/track_zones.h"
#include "nfsu/prop_settling.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>

namespace nfsu {
float RaceSession::next_world_frame_seconds()const noexcept {
    return float(kDriveFrameSeconds)*crash_camera_.scale(float(kDriveFrameSeconds));
}
bool RaceSession::set_crash_camera(const CrashCameraRoadSource* source,unsigned intensity,unsigned track,std::string& error) {
    if(started_||intensity>255){error="crash camera must be configured before start, intensity 0..255";return false;}
    crash_source_=intensity?source:nullptr;crash_camera_.configure(track,intensity);error.clear();return true;
}
bool RaceSession::set_traffic(const std::filesystem::path& root,const std::vector<RaceTrafficSetup>& setup,std::string& error,unsigned density,std::uint32_t* random) {
    if(started_||traffic_){error="traffic must be configured before start";return false;}
    std::unique_ptr<RaceTraffic> next;
    if(!setup.empty()) {
        next=std::make_unique<RaceTraffic>();
        if(!next->load(root,course_,setup,error,density,random,drag_))return false;
    }
    auto props=std::make_unique<RaceProps>();
    if(!props->load(root,error,course_.name))return false;
    // Only the world owner has traffic caches. Rival sessions must not each
    // reserve the maximum campaign traffic pool. Allocation stays before start.
    world_track_caches_.resize(4+std::max(std::size_t(6),setup.size()));
    contact_traffic_epochs_.resize(setup.size());
    near_pass_cache_.resize(3+setup.size());
    if(!prepare_world_motion(root,error))return false;
    props_=std::move(props);traffic_=std::move(next);return true;
}

namespace {
void u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned k = 0; k < 4; ++k) out.push_back(static_cast<std::uint8_t>(value >> (8*k)));
}
void f32(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits; std::memcpy(&bits, &value, sizeof bits); u32(out, bits);
}
double speed(const VehicleState& state) {
    return vehicle_sqrt(state.velocity.x*state.velocity.x + state.velocity.y*state.velocity.y);
}
double forward_speed(const VehicleState& state) {
    const auto& q = state.orientation;
    return state.velocity.x*(1-2*(q.y*q.y+q.z*q.z)) +
           state.velocity.y*2*(q.x*q.y+q.w*q.z) +
           state.velocity.z*2*(q.x*q.z-q.w*q.y);
}
}

RaceSession::RaceSession(const VehicleTrackCourse& course)
    : RaceSession(course, stock_240sx_vehicle_spec()) {}

RaceSession::RaceSession(const VehicleTrackCourse& course, const VehicleSpec& spec, RaceSteering steering,
                         const VehicleHull& hull, RacePresentation presentation)
    : course_(course), spec_(spec), model_(spec_),
      // The baseline input recordings deliberately retain their finite-ray adapter.
      // Native player controls and selector3 rivals use440A40 strut heights.
      vehicle_(model_,course_,vehicle_track_first_lap_approximations(),hull,
               steering==RaceSteering::baseline&&presentation==RacePresentation::player?
                   TrackStrutQuery::segment:TrackStrutQuery::cached_height),
      chase_(stock_240sx_chase_camera_table(), {}, {}), camera_world_(course.world),
      presentation_(presentation), steering_mode_(steering),
      contact_box_(race_contact_box(hull)),contact_bounds_(race_track_body_bounds(spec_,contact_box_)) {
    world_objects_.resize(course_.world_objects.size());
    for(unsigned i=0;i<world_objects_.size();++i)
        if(course_.world_objects[i].fixed_collision)initialize_world_object(i);
    //57F2B0 ride424 is also consumed by ordinary479640, before any drag setup.
    float performance_area=0,performance_grip=0;
    if(ai_setup_performance(spec_.handling,performance_area,performance_grip))
        drag_category_=performance_area>500?0U:performance_area>390?1U:performance_area>280?2U:3U;
    //460930 obtains the ride's sampled acceleration/drag ceiling from580240.
    float predicted_speed=0.F;
    if(steering_mode_!=RaceSteering::baseline&&
       ai_setup_predicted_speed(spec_.handling,predicted_speed)&&predicted_speed>0.F)
        steering_max_speed_=predicted_speed;
}

RaceSession::~RaceSession()=default;
bool RaceSession::set_camera_table(const ChaseCameraTable& table,std::string& error) {
    if(started_){error="camera table must precede start";return false;}
    ChaseCamera selected(table,{},{});
    if(!selected.valid()){error=selected.error();return false;}
    chase_=selected;return true;
}
bool RaceSession::set_lap_knockout(bool enabled,std::string& error,unsigned laps) {
    if(started_){error="knockout policy must precede start";return false;}
    if(enabled && (course_.route.point_to_point || laps<1 || laps>3 || unsigned(course_.route.laps)!=laps)) {
        error="knockout requires one circuit lap per opponent";return false;
    }
    lap_knockout_=enabled;error.clear();return true;
}
bool RaceSession::set_time_trial(std::int32_t ticks,std::string& error) {
    if(started_||ticks<=0||ticks>kRaceTicksLimit||!course_.route.point_to_point) {
        error="time trial requires a fresh point-to-point session and positive deadline";return false;
    }
    time_limit_ticks_=ticks;error.clear();return true;
}
unsigned RaceSession::player_position() const {
    if(spec_.drift_mode)return drift_competition_.position();
    if(timing_.racers.empty())return 1;
    if(timing_.racers[0].position>0)return unsigned(timing_.racers[0].position);
    return standings_.position(0);
}
bool RaceSession::player_won() const {
    if(timing_.racers.empty()||!timing_.racers[0].finish_valid||
       timing_.racers[0].status!=RacerStatus::normal_completion||player_position()!=1)return false;
    //5A3098 is a reward predicate, not a writer of the race's place.
    return !is_time_trial()||timing_.racers[0].finish_ticks<time_limit_ticks_;
}
std::array<unsigned,4> RaceSession::result_positions() const noexcept {
    std::array<unsigned,4> places{};std::array<bool,5> used{};
    const unsigned count=std::min(4U,unsigned(timing_.racers.size()));
    //Terminal records own their assigned places. In particular a blown
    //engine may reserve last place before the live standings refresh.
    for(unsigned actor=0;actor<count;++actor) {
        const int place=timing_.racers[actor].position;
        if(place>0&&unsigned(place)<=count&&!used[unsigned(place)]) {
            places[actor]=unsigned(place);used[unsigned(place)]=true;
        }
    }
    for(unsigned rank=0;rank<count;++rank) {
        const unsigned actor=standings_.order[rank];
        if(actor>=count||places[actor])continue;
        unsigned place=1;while(place<=count&&used[place])++place;
        places[actor]=place;used[place]=true;
    }
    return places;
}
std::vector<RaceResultRow> RaceSession::result_rows() const {
    auto rows=race_timing_results(course_.route,timing_);
    const auto places=result_positions();
    for(auto& row:rows)row.position=int(places.at(row.racer));
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.position<b.position;});
    return rows;
}
bool RaceSession::prepare_leaderboard(const std::filesystem::path& root,const std::string& player_car,std::string& error) {
    if(started_ || rivals_.empty() || rivals_.size()>3){error="leaderboard requires fresh race roster";return false;}
    float area=0,grip=0;
    if(!ai_setup_performance(spec_.handling,area,grip)){error="leaderboard performance class";return false;}
    const unsigned category=area>500?0U:area>390?1U:area>280?2U:3U;
    std::vector<unsigned char> image;RacingLineTrack roads;
    auto track=load_race_route_track(root/"Tracks"/course_.name,image);
    RaceGapMap map;std::array<std::string,4> names{{player_car,player_car,player_car,player_car}},labels;
    for(unsigned i=0;i<rivals_.size();++i)names[i+1]=rival_setup_[i].car_type;
    if(!track.ok() || !parse_racing_line_image(image.data(),image.size(),{},roads) ||
       !map.build(course_.track,course_.route,roads,category)){error="leaderboard original road time map";return false;}
    if(!load_race_car_labels(root,names,labels,error))return false;
    gap_map_=map;car_labels_=std::move(labels);return true;
}
std::string RaceSession::leaderboard_label(unsigned actor) const {
    if(actor>rivals_.size())return {};
    // 4A3930 fills the human name from the active profile for every display mode.
    if(actor==0)return player_name_;
    if(!gap_map_.valid || leaderboard_cycle_.shown==RaceLeaderboardMode::names)return rival_name(actor-1);
    if(leaderboard_cycle_.shown==RaceLeaderboardMode::cars)return car_labels_[actor];
    if(timing_.racers[actor].status==RacerStatus::knocked_out)return "Knocked Out";
    if(timing_.racers[actor].status==RacerStatus::blown_engine)return "Blown Engine";
    if(timing_.racers[actor].status==RacerStatus::totalled)return "Totaled";
    if(timing_.racers[actor].finish_valid)return "Finished";
    if(!timing_.go_valid)return race_gap_text(0);
    return race_gap_text(race_gap_ticks(gap_map_,gap_actors_[actor],gap_actors_[0],timing_.now_ticks-timing_.go_ticks));
}
void RaceSession::update_standings() {
    std::array<RaceStandingSample,4> samples{};
    const auto score=[&](const RaceProgress& p) {
        double percent=p.previous_percent-course_.route.boundaries.at(std::size_t(course_.route.finish)).percent;
        if(course_.route.point_to_point){if(percent< -50)percent+=100;else if(percent>50)percent-=100;}
        if(percent<0)percent+=100;
        // -1 is the approach to the starting line, not lap zero. Clamping it
        // ranks a car still behind the line ahead of one that just crossed.
        return p.lap*100+percent;
    };
    for(unsigned i=0;i<=rivals_.size();++i) {
        const auto& r=i?rival(i-1):*this;
        const auto& timing=timing_.racers[i];
        samples[i]={score(r.progress()),unsigned(std::max(0,timing.position))};
        // Finished actors continue physically; their competitive progress
        // stays at the finish, like the retained original lap record.
        if(timing.finish_valid)samples[i].progress=timing.highest_lap*100.0;
        else if(lap_knockout_&&timing.status==RacerStatus::knocked_out)
            samples[i].progress=double(gap_actors_[i].progress)*100.0;
        gap_actors_[i]={float(samples[i].progress*.01),timing.finish_valid,timing.highest_lap,timing.finish_ticks};
    }
    standings_.update(samples,unsigned(rivals_.size()+1));
    leaderboard_cycle_.update(timing_.now_ticks);
}
bool RaceSession::set_rivals(const std::vector<RaceRivalSetup>& setup,std::string& error,
                             RacePresentation presentation,unsigned player_grid) {
    return set_rivals_impl(setup,error,presentation,nullptr,player_grid);
}
bool RaceSession::set_human_rival(const RaceRivalSetup& setup,std::string& error) {
    if(drag_||spec_.drift_mode||lap_knockout_||time_limit_ticks_) {
        error="LAN prototype requires an ordinary circuit";return false;
    }
    // Construct with the same player strut-query policy, then suppress only
    // presentation. A baseline headless AI constructor uses a different query.
    if(!set_rivals_impl({setup},error,RacePresentation::player,nullptr))return false;
    rivals_[0]->presentation_=RacePresentation::none;
    human_rival_=lan_player_=true;rivals_[0]->lan_player_=true;
    return true;
}
bool RaceSession::advance_humans(const std::array<GamepadFrame,2>& input,std::string& error) {
    if(!human_rival_||rivals_.size()!=1||planned_rivals_) {
        error="two-human world is not configured";return false;
    }
    auto pads=input;
    // Validate the complete pair before mutating either actor.
    for(auto& frame:pads) {
        const auto& pad=frame.pads[0];
        if(pad.buttons&~kGamepadButtonMask) {error="unknown LAN input button";return false;}
        for(auto axis:pad.axes)if(axis<kAxisMin||axis>kAxisMax) {error="invalid LAN input axis";return false;}
        frame.time_us=timing_.step*1000000/60;
        frame.sample_clock_available=false;
    }
    for(unsigned i=0;i<2;++i)if(timing_.racers.size()>i&&timing_.racers[i].status!=RacerStatus::not_finished) {
        pads[i].pads[0]={};pads[i].pads[0].connected=true;
        pads[i].pads[0].buttons=gamepad_button_bit(GamepadButton::trigger_left);
    }
    return advance_impl(&pads[0],nullptr,error,-1,nullptr,nullptr,nullptr,&pads[1]);
}
bool RaceSession::set_drag(bool enabled,std::string& error,float shift_scale) {
    if(started_||!rivals_.empty()||!std::isfinite(shift_scale)||shift_scale<=0) {
        error="drag mode or shift scale invalid before grid setup";return false;
    }
    if(enabled) {
        float area=0,grip=0;
        if(!ai_setup_performance(spec_.handling,area,grip)){error="drag performance category";return false;}
        drag_category_=area>500?0U:area>390?1U:area>280?2U:3U;
        // 476D8F uses the difficulty scale outside Underground mode.
        drag_shift_scale_=shift_scale;
    }
    drag_=enabled;error.clear();return true;
}
bool RaceSession::set_planned_rivals(const std::vector<RaceRivalSetup>& setup,
                                    const std::array<float,3>& low_slip,std::string& error,unsigned player_grid) {
    return set_rivals_impl(setup,error,RacePresentation::none,&low_slip,player_grid);
}
bool RaceSession::set_rivals_impl(const std::vector<RaceRivalSetup>& setup,std::string& error,
                                 RacePresentation presentation,const std::array<float,3>* low_slip,unsigned player_grid) {
    if(started_||!rivals_.empty()||setup.empty()||setup.size()>3){error="one to three rivals required before start";return false;}
    if(player_grid==0||player_grid>setup.size()+1){error="player grid outside the collision roster";return false;}
    for(const auto& r:setup) {
        if(r.hull.points.empty()||r.name.empty()||r.ai_mode>2){error="rival needs selected collision hull, identity and native AI mode";return false;}
    }
    std::vector<std::unique_ptr<RaceSession>> cars;
    std::vector<std::unique_ptr<RaceRouteDriver>> drivers;
    for(std::size_t i=0;i<setup.size();++i) {
        const auto& r=setup[i];
        auto car=std::make_unique<RaceSession>(course_,r.spec,steering_mode_,r.hull,presentation);
        car->external_contact_owner_=true;
        if(drag_&&!car->set_drag(true,error))return false;
        car->drag_shift_scale_=1.F; //Original AI quality windowD8 constructor default.
        if(low_slip&&!drag_&&!car->configure_ai((*low_slip)[i],error))return false;
        cars.push_back(std::move(car));
        if(!low_slip)drivers.push_back(std::make_unique<RaceRouteDriver>(RaceDrivingLine::event1_forward));
    }
    rival_setup_=setup;rivals_=std::move(cars);rival_drivers_=std::move(drivers);
    planned_rivals_=low_slip!=nullptr;player_grid_=player_grid;
    error.clear();return true;
}

unsigned RaceSession::registration_rank(unsigned actor) const noexcept {
    // 422A70 constructs racers by grid slot. 41FE50 subsequently publishes
    // the human first, but 468680 still uses construction IDs for equal X.
    if(actor==0)return player_grid_-1;
    return actor<player_grid_?actor-1:actor;
}
bool RaceSession::steer_input(VehicleInput& input, int substep, std::string& error) {
    const auto& s = car(); const auto& q = s.orientation;
    const double fx = 1-2*(q.y*q.y+q.z*q.z), fy = 2*(q.x*q.y+q.w*q.z);
    const std::uint16_t difference = static_cast<std::uint16_t>(
        chase_angle(fx,fy)-chase_angle(s.velocity.x,s.velocity.y));
    SteeringContext context;
    context.raw = float(input.steer);
    context.seconds = float(1+(crash_source_?steering_seconds_:double(steering_steps_)/60));
    context.dt = static_cast<float>(frame_seconds_);
    context.forward_speed = float(forward_speed(s));
    context.speed = float(vehicle_sqrt(s.velocity.x*s.velocity.x+s.velocity.y*s.velocity.y+s.velocity.z*s.velocity.z));
    context.heading_minus_velocity_deg = float(double(static_cast<std::int16_t>(difference))*(360.0/65536));
    // Traced consumers are processed controls, not separate digital/analog
    // sources. Use the applied port values, including reverse assist. The
    // native speed/gear brake normalizer itself remains unrecovered here.
    context.throttle = float(input.throttle);
    context.handbrake = input.handbrake ? 1 : 0;
    context.brake = float(input.brake);
    context.predicted_max_speed = steering_max_speed_;
    context.shift_elapsed = shifted_ ? float(crash_source_?steering_seconds_-shift_seconds_:double(steering_steps_-shift_step_)/60) : 1;
    context.predicate = steering_mode_ == RaceSteering::recovered_predicate1;
    if (!steering_step(context,steering_state_,steering_result_)) {
        error = "recovered steering context refused at substep "+std::to_string(substep); return false;
    }
    input.road_steering = true;
    input.road_angle_word_valid = true;
    input.road_angle_word = steering_result_.angle;
    input.road_angle = double(steering_result_.angle_deg)*(3.14159265358979323846/180);
    input.road_target = double(steering_result_.target_deg)*(3.14159265358979323846/180);
    input.steer_history = steering_result_.filtered;
    input.steer_limit_deg=steering_result_.limit_deg; input.steer_rate_deg_s=steering_result_.rate_deg_s;
    input.steer_shift_factor=steering_result_.shift_factor; input.steering_mode=int(steering_mode_);
    return true;
}

double RaceCameraWorld::clear_fraction(const VehicleVec3& from, const VehicleVec3& to) const {
    const float a[3] = {float(from.x),float(from.y),float(from.z)};
    const float b[3] = {float(to.x),float(to.y),float(to.z)};
    const double x=double(b[0])-a[0], y=double(b[1])-a[1], z=double(b[2])-a[2];
    const double length=vehicle_sqrt(x*x+y*y+z*z);
    if (length==0) return 1;
    TrackContact hit;
    const auto status=query_track_contact_segment(world_,a,b,hit);
    if (status==TrackContactStatus::refused) return 0;
    return status==TrackContactStatus::hit ? std::clamp(double(hit.distance)/length,0.0,1.0) : 1.0;
}

bool RaceCameraWorld::ground(const AiDriverPoint& point,float& z)const {
    if(!world_.valid||!std::all_of(point.begin(),point.end(),[](float f){return std::isfinite(f);}))return false;
    TrackContact hit;
    //429CF0 ignores440A40's optional hit flag: a valid cache miss returns
    //height0 rather than aborting the predicted flight. Use its ordered,
    //retained polygon selector and second (unbounded-height) pass as shadows do.
    z=query_track_ground(world_,{point[0],point[1],point[2]},ground_cache_,hit)?hit.point[2]:0.F;
    return true;
}
bool RaceSession::skip_start_cinematic() {
    if(!start_cinematic() || !set_race_stage(timing_,RaceStage::countdown,timing_events_))return false;
    // Advance the authored scene to its release frame. The race clock and
    // physics keep their current time, and GO still requires the countdown.
    cinematic_skip_ticks_=std::max(0,cinematic_end_ticks_-timing_.now_ticks);
    for(auto& rival:rivals_)rival->skip_start_cinematic();
    return true;
}
bool RaceSession::start(std::string& error,const VehiclePose* pose,float countdown_step,float cinematic_seconds) {
    crash_camera_.reset();frame_seconds_=kDriveFrameSeconds;steering_seconds_=shift_seconds_=visual_frames_=0;
    prop_contacts_={};
    if (started_) { error = "race session is one-shot"; return false; }
    if (!controls_.configure(drag_?default_drag_controls_config():default_drive_controls_config(),spec_.nitrous.installed,error)) return false;
    if (spec_.nitrous.installed &&
        !nitrous_resource_.initialize(spec_.nitrous.capacity_seconds,spec_.nitrous.count)) {
        error="installed nitrous resource refused";return false;
    }
    if (steering_mode_ != RaceSteering::baseline && steering_mode_ != RaceSteering::recovered_predicate0 &&
        steering_mode_ != RaceSteering::recovered_predicate1) { error="invalid steering mode"; return false; }
    if (!course_.has_route || !vehicle_.valid() || !vehicle_.start(pose?*pose:course_.start)) {
        error = "race course/vehicle: " + vehicle_.error(); return false;
    }
    if(!std::isfinite(countdown_step) || countdown_step<=0 || double(countdown_step)*180>=std::numeric_limits<unsigned>::max()) {
        error="invalid grid countdown interval";return false;
    }
    if(!std::isfinite(cinematic_seconds)||cinematic_seconds<0||cinematic_seconds>60) {
        error="invalid start cinematic interval";return false;
    }
    cinematic_end_ticks_=kRaceWorldStartTicks+std::int32_t(std::ceil(cinematic_seconds*kRaceTicksPerSecond));
    if(!vehicle_.prepare_grid(static_cast<unsigned>(std::ceil(countdown_step*3*60)))) {
        error="grid physical preparation: "+vehicle_.error();return false;
    }
    for(std::size_t i=0;i<rivals_.size();++i)
        if(!rivals_[i]->start(error,&rival_setup_[i].pose,countdown_step,cinematic_seconds))return false;
    if (!make_race_timing_config(60, 1+rivals_.size(), timing_config_, error))return false;
    if(human_rival_)timing_config_.humans=2;
    timing_config_.countdown_step_seconds=countdown_step;
    timing_config_.time_limit_ticks=time_limit_ticks_;
    if (!start_race_timing(course_.route, timing_config_, timing_, error)) return false;
    set_race_stage(timing_, cinematic_seconds>0?RaceStage::prerace:RaceStage::countdown, timing_events_);
    update_standings();

    //4B47A0 selects the fourth configured POV on entering drift.
    if(!chase_.set_pov(spec_.drift_mode?4:0)) {
        error="race camera anchor missing for selected mode";return false;
    }
    if (presentation_==RacePresentation::player &&
        !chase_.reset(chase_subject(car(), spec_), camera_world_, view_)) {
        error = chase_.error(); return false;
    }
    camera_motion_.reset();presented_pov_=view_.pov;presented_crash_=0;presented_look_back_=false;
    camera_motion_.update({{float(view_.eye.x),float(view_.eye.y),float(view_.eye.z)}},float(kDriveFrameSeconds),true);
    // LAN controllers are already connected in the lobby. Prime that
    // connection without advancing time or suppressing a held launch pedal.
    if(lan_player_) {GamepadFrame neutral;neutral.pads[0].connected=true;controls_.update(neutral);}
    started_ = true;
    return true;
}

bool RaceSession::advance(const GamepadFrame& pad, std::string& error,
                           RaceSimulationProfile* profile,TimingService* timer) {
    if(human_rival_) {error="LAN world requires both human inputs";return false;}
    return advance_impl(&pad,nullptr,error,-1,nullptr,profile,timer);
}
#include "race_finish_probe.inc"
bool RaceSession::advance_planned(const GamepadFrame& pad,const RaceRivalPlans& plans,std::string& error,
                                  RaceSimulationProfile* profile,TimingService* timer) {
    const bool solo_drag=drag_&&rivals_.empty()&&plans.drag_steering&&
        plans.count==0&&plans.step==timing_.step;
    const bool solo_finish=spec_.drift_mode&&rivals_.empty()&&timing_.outcome==RaceOutcome::finished&&
        plans.count==0&&plans.step==timing_.step;
    if(!planned_rivals_&&!solo_drag&&!solo_finish){error="planned rivals are not configured";return false;}
    return advance_impl(&pad,nullptr,error,-1,&plans,profile,timer);
}

bool RaceSession::advance_driver(const VehicleInput& input, std::string& error,int ai_gear_request) {
    if(presentation_!=RacePresentation::none) {
        error="direct driver requires a car without player presentation";return false;
    }
    if((ai_gear_request!=-1&&ai_gear_request!=0)||(!ai_physics_&&ai_gear_request!=-1)) {
        error="processed AI gear request requires selector3 automatic/reverse";return false;
    }
    return advance_impl(nullptr,&input,error,ai_gear_request);
}

bool RaceSession::configure_ai(float low_slip,std::string& error) {
    if(started_||presentation_!=RacePresentation::none||!vehicle_.configure_ai(low_slip)) {
        error="selector3 requires a configured headless car before start";return false;
    }
    ai_physics_=true;error.clear();return true;
}

void RaceSession::visit_actor_contacts(unsigned actor,void* context,
    void (*visit)(void*,const ContactRecord&)) const {
    if(!visit||actor>rivals_.size())return;
    for(const auto& pair:world_contact_pairs_)if(pair.record.active&&
        (pair.a==actor||(pair.cars()&&pair.b==actor)))visit(context,pair.record);
}
RaceDriverWallContact RaceSession::driver_wall_contact(unsigned actor) const noexcept {
    RaceDriverWallContact out;
    for(const auto& pair:world_contact_pairs_) {
        if(!pair.record.active||!pair.track()||pair.a!=actor||
           pair.polygon>=course_.world.collision.polygons.size())continue;
        //4534F0/45F1BD distinguish the kind3 vertical plane from ground.
        if(course_.world.triangles[2*pair.polygon].up>=.1F)continue;
        out.active=true;
        const auto elapsed=std::uint32_t(timing_.now_ticks)-pair.record.creation_tick;
        std::int32_t signed_elapsed;std::memcpy(&signed_elapsed,&elapsed,4);
        out.recent_strong|=float(signed_elapsed)*.00025F<.2F&&pair.record.magnitude_sum>10.F;
    }
    return out;
}

void RaceSession::visit_actor_pairs(unsigned actor,void* context,
    void (*visit)(void*,unsigned,unsigned,const ContactRecord&)) const {
    if(!visit||actor>rivals_.size())return;
    for(const auto& pair:world_contact_pairs_)if(pair.record.active&&pair.cars()&&
        (pair.a==actor||pair.b==actor))visit(context,pair.a,pair.b,pair.record);
}

bool RaceSession::advance_impl(const GamepadFrame* pad,const VehicleInput* direct,std::string& error,int ai_gear_request,
                               const RaceRivalPlans* plans,RaceSimulationProfile* profile,TimingService* timer,
                               const GamepadFrame* remote_pad) {
    if(profile && !timer){error="simulation profiling requires a clock";return false;}
    auto profile_start=profile?timer->now_us():0;
    const auto stage=[&](double RaceSimulationProfile::*field) {
        if(!profile)return;
        const auto end=timer->now_us();profile->*field=(end-profile_start)/1000.0;profile_start=end;
    };
    // A finished rival remains a physical actor while the human is racing.
    // Timing already freezes each finished racer's result. Freezing its body
    // instead leaves a collidable stack on the finish line.
    const bool finished_ai=(ai_physics_||drag_)&&direct&&timing_.outcome==RaceOutcome::finished;
    const bool finished_human=pad&&plans&&presentation_==RacePresentation::player&&timing_.outcome==RaceOutcome::finished;
    const bool finished_lan=lan_player_&&pad&&timing_.outcome==RaceOutcome::finished;
    if(human_rival_&&!remote_pad) {error="missing remote human input";return false;}
    if (!started_ || (timing_.outcome != RaceOutcome::running&&!finished_ai&&!finished_human&&!finished_lan)) {
        error = "race session is not running"; return false;
    }
    crash_camera_.begin_frame(float(kDriveFrameSeconds));
    frame_seconds_=external_frame_seconds_>0?external_frame_seconds_:
        (crash_camera_.event()?double(float(kDriveFrameSeconds)*.4F):kDriveFrameSeconds);
    const float world_dt=float(frame_seconds_);
    const unsigned solver_slices=unsigned(world_dt/.004399999976158142F)+1;
    const double solver_dt=frame_seconds_/solver_slices;
    if(ai_physics_&&!direct) {error="selector3 needs processed AI controls";return false;}
    if(start_cinematic() && timing_.now_ticks>=cinematic_end_ticks_)
        set_race_stage(timing_,RaceStage::countdown,timing_events_);
    const bool held_grid=race_car_held(timing_);
    vehicle_.defer_body_contacts(true);
    if(traffic_)traffic_->defer_body_contacts(true);
    if(planned_rivals_&&!held_grid&&(!plans||plans->step!=timing_.step||plans->count!=rivals_.size())) {
        error="current complete rival plans required";return false;
    }
    std::array<RaceContactPose,4> previous_poses{};
    std::array<std::uint64_t,4> previous_resets{};
    previous_poses[0]=race_contact_pose(car());previous_resets[0]=telemetry().resets;
    for(std::size_t i=0;i<rivals_.size();++i) {
        previous_poses[i+1]=race_contact_pose(rivals_[i]->car());
        previous_resets[i+1]=rivals_[i]->telemetry().resets;
    }
    std::array<VehicleInput,3> rival_controls{};
    if(planned_rivals_&&!held_grid) {
        for(std::size_t i=0;i<rivals_.size();++i)
            if(!std::isfinite(plans->cars[i].physical_scale)||plans->cars[i].physical_scale<1.F) {
                error="invalid rival catch-up scale";return false;
            }
        auto next_drivers=original_drivers_;
        for(std::size_t i=0;i<rivals_.size();++i) {
            const auto& r=*rivals_[i];
            if(r.timing().outcome!=RaceOutcome::running&&r.timing().outcome!=RaceOutcome::finished)continue;
            const auto& p=plans->cars[i];const auto& s=r.car();const auto& q=s.orientation;
            if(p.reset_car)rivals_[i]->vehicle_.request_reset();
            // A newly placed car has not yet imported its component body. Its
            // public pose is authoritative until the first physical frame.
            const AiDriverPoint forward=s.rigid_body_ready?s.rigid_body.axes[0]:AiDriverPoint{{
                float(1-2*(q.y*q.y+q.z*q.z)),float(2*(q.x*q.y+q.w*q.z)),float(2*(q.x*q.z-q.w*q.y))}};
            AiDriverForwardInput in;in.path=p.path;in.desired_speed=p.desired_speed;
            in.throttle_cap=p.throttle_cap;in.route_distance=p.route_distance;in.category=p.driver_category;in.enabled=p.enabled;
            in.position={float(s.position.x),float(s.position.y),float(s.position.z)};
            in.velocity={float(s.velocity.x),float(s.velocity.y),float(s.velocity.z)};
            in.heading=track_angle(forward[0],forward[1]);
            in.forward_speed=(in.velocity[0]*forward[0]+in.velocity[1]*forward[1])+in.velocity[2]*forward[2];
            in.absolute_speed=float(vehicle_sqrt(double(in.velocity[0])*in.velocity[0]+
                double(in.velocity[1])*in.velocity[1]+double(in.velocity[2])*in.velocity[2]));
            in.rear_drive_fraction=r.spec().handling.rear_drive_fraction;
            in.race_seconds=timing_.race_start_valid?float(timing_.now_ticks-timing_.race_start_ticks)*.00025F:-1.F;
            for(unsigned k=0;k<4;++k){in.contact[k]=s.wheels[k].contact;in.wheel_slip[k]=s.wheels[k].prepared_slip;}
            if((p.recovery_cleanup&&!ai_driver_recovery_reset(in,next_drivers[i]))||
               !(drag_?ai_driver_drag_step(in,world_dt,next_drivers[i]):
                       ai_driver_ordinary_step(in,world_dt,next_drivers[i]))) {
                error="original rival driver refused slot "+std::to_string(i);return false;
            }
            auto& control=rival_controls[i];const auto& driver=next_drivers[i];
            control.throttle=driver.pedals.throttle;control.brake=driver.pedals.brake;
            control.race_active=true;
            control.drag_world_random=drag_?plans->drag_world_random:nullptr;
            control.road_steering=true;control.road_angle_word_valid=true;control.road_angle_word=driver.steering.word;
            control.road_angle=double(static_cast<std::int16_t>(driver.steering.word))*(6.28318530717958647692/65536);
            control.road_target=control.road_angle;
        }
        // Validate every plan before advancing any body or committing any history.
        for(std::size_t i=0;i<rivals_.size();++i)if(rivals_[i]->ai_physics_ &&
            !rivals_[i]->vehicle_.set_ai_scale(plans->cars[i].physical_scale)) {
            error="rival catch-up physical publication refused";return false;
        }
        original_drivers_=next_drivers;
    }
    if(drag_&&held_grid&&planned_rivals_) {
        if(!plans||plans->step!=timing_.step||plans->count!=rivals_.size()) {
            error="current drag launch plans required";return false;
        }
        for(unsigned i=0;i<rivals_.size();++i)rival_controls[i].throttle=plans->cars[i].grid_throttle;
    }
    drafting_=0;
    if(!held_grid&&!spec_.drift_mode) {
        const std::array<float,3> follower{{float(car().position.x),float(car().position.y),float(car().position.z)}};
        const auto add_draft=[&](const VehicleState& leader){
            drafting_+=race_drafting_pair({float(leader.position.x),float(leader.position.y),float(leader.position.z)},
                {float(leader.velocity.x),float(leader.velocity.y),float(leader.velocity.z)},leader.owner_speeds.magnitude,follower);
            drafting_=std::min(drafting_,.8999999761581421F);
        };
        for(const auto& r:rivals_)add_draft(r->car());
        if(traffic_)for(const auto& t:traffic_->contact_actors())if(t.active)add_draft(t.car);
        const float human_draft=drafting_;
        for(unsigned i=0;i<rivals_.size();++i){
            const auto& c=rivals_[i]->car();
            const std::array<float,3> target{{float(c.position.x),float(c.position.y),float(c.position.z)}};
            const auto from=[&](const VehicleState& lead){return race_drafting_pair(
                {float(lead.position.x),float(lead.position.y),float(lead.position.z)},
                {float(lead.velocity.x),float(lead.velocity.y),float(lead.velocity.z)},lead.owner_speeds.magnitude,target);};
            float amount=from(car());
            for(unsigned j=0;j<rivals_.size();++j)if(i!=j)amount+=from(rivals_[j]->car());
            if(traffic_)for(const auto& t:traffic_->contact_actors())if(t.active)amount+=from(t.car);
            rival_controls[i].drafting=std::min(amount,.8999999761581421F);
        }
        drafting_=human_draft;
    }
    if(lan_player_&&external_human_drafting_>=0)drafting_=external_human_drafting_;
    ++work_.frames;
    if(pad) {controls_.update(*pad);++work_.pad_updates;}
    else ++work_.direct_updates;
    const DriveFrameState& frame = controls_.frame_state();
    service_brake_=direct?direct->brake:(frame.brake_from_button?frame.brake_digital:frame.brake_analog);
    animation_.braking=service_brake_>0 || (direct?direct->handbrake:frame.handbrake);
    std::array<RaceTimingInput,4> timing_inputs{};
    auto& timing_input=timing_inputs[0];
    stage(&RaceSimulationProfile::drivers);
    //The world continues traffic motion during racer staging/countdown.
    //Holding grid cars must not postpone traffic admission/movement until GO.
    if(!external_contact_owner_) {
        if(traffic_) {
            std::array<const VehicleState*,4> racers{{&car()}};
            for(unsigned i=0;i<rivals_.size();++i)racers[i+1]=&rivals_[i]->car();
            const auto begin=profile?timer->now_us():0;
            std::array<bool,4> ghosts{};
            for(unsigned i=0;i<racers.size();++i)
                ghosts[i]=racers[i]&&timing_.racers.at(i).permanent_ghost;
            std::array<const RaceContactBox*,4> shapes{{&contact_box_}};
            for(unsigned i=0;i<rivals_.size();++i)shapes[i+1]=&rivals_[i]->contact_box_;
            if(!traffic_->prepare_frame(racers,timing_.step,error,timing_.now_ticks,world_dt,ghosts,shapes)){started_=false;return false;}
            if(profile)profile->traffic_motion=(timer->now_us()-begin)/1000.0;
        }
    }
    const auto advance_rival=[&](std::size_t i) {
        auto& r=*rivals_[i];r.external_frame_seconds_=frame_seconds_;
        if(r.timing().outcome==RaceOutcome::running ||
           ((planned_rivals_||human_rival_)&&r.timing().outcome==RaceOutcome::finished)) {
            bool ok;
            if(planned_rivals_) {
                ok=r.advance_driver(rival_controls[i],error,drag_?-1:original_drivers_[i].gear_request);
                if(!held_grid)++work_.rival_driver_updates;
            } else if(human_rival_) {
                r.external_human_drafting_=float(rival_controls[i].drafting);
                ok=r.advance(*remote_pad,error);
            }
            else ok=r.advance(rival_drivers_[i]->sample(r),error);
            if(!ok){error="rival "+std::to_string(i)+": "+error;started_=false;return false;}
        }
        if(r.timing().outcome==RaceOutcome::running || r.timing().step==timing_.step+1) {
            timing_inputs[i+1].events=r.vehicle_.events().data();
            timing_inputs[i+1].event_count=r.vehicle_.events().size();
            timing_inputs[i+1].speed=float(speed(r.car()));
        }
        return true;
    };
    const auto advance_player=[&]() {
    const bool countdown=timing_.stage==RaceStage::countdown;
    if(!held_grid||countdown)nitrous_clock_=static_cast<std::uint32_t>(timing_.now_ticks);
    if((!held_grid||countdown)&&!direct&&!finished_human) {
        // 431C0F starts the resource before branching on stage; 42ABF0
        // awards the discrete purge while the grid still holds the car.
        const auto transition=nitrous_dispatch(nitrous_resource_,nitrous_clock_,controls_.held(),{});
        nitrous_engine_.update(nitrous_resource_,nitrous_clock_);
        if(countdown&&transition==NitrousTransition::started)style_.trigger(14,1.F,timing_.now_ticks);
        if(!held_grid&&controls_.frame_state().reset) {vehicle_.request_reset();crash_camera_.reset();}
    }
    if (!held_grid) {
        const bool recovered = !direct && !drag_ && steering_mode_ != RaceSteering::baseline;
        // Keep the legacy processed-controls adapter at two120Hz updates.
        // Human4615E0 calls steering460930 once with the world dt before
        // physics. Calling it twice with half dt incorrectly snaps each
        // small slew below one degree back to zero during gradual turn-in.
        const bool owner_pedals=recovered&&car().gearbox==VehicleGearbox::automatic;
        const double longitudinal=owner_pedals?car().owner_speeds.forward:forward_speed(car());
        auto first=direct?*direct:controls_.step(longitudinal,car().gear,car().gearbox,recovered,owner_pedals);
        auto held=direct||owner_pedals?first:controls_.step(longitudinal,car().gear,car().gearbox,recovered);
        held.race_active=true;held.drafting=direct?direct->drafting:drafting_;
        drafting_=held.drafting;
        held.drag_mode=drag_;held.drag_clock=timing_.now_ticks;
        held.drag_category=drag_category_;held.drag_shift_scale=drag_shift_scale_;
        if(plans&&drag_)held.drag_world_random=plans->drag_world_random;
        if(!direct && car().gear==kVehicleGearNeutral &&
            held.direction_request!=VehicleDirectionRequest::reverse)
            held.throttle=drive_neutral_throttle(float(held.throttle),float(inputs_[kDriveStepsPerFrame-1].throttle),
                float(car().engine_rpm),spec_.handling.idle_rpm,world_dt);
        if(!direct) {
            held.nitrous_active=nitrous_engine_.active;
            held.nitrous_clock=nitrous_clock_;
            held.nitrous_activation_seconds=nitrous_engine_.activation_seconds;
        }
        if(recovered) {
            if(!steer_input(held,1,error)) {started_=false;return false;}
            ++steering_steps_;steering_seconds_+=frame_seconds_;
        }
        if(drag_&&!direct&&!finished_human) {
            if(!plans||!plans->drag_steering){error="current drag steering path required";return false;}
            const auto& p=plans->human;const auto& s=car();const auto& q=s.orientation;
            AiDriverForwardInput in;in.path=p.path;in.desired_speed=p.desired_speed;
            in.position={float(s.position.x),float(s.position.y),float(s.position.z)};
            in.velocity={float(s.velocity.x),float(s.velocity.y),float(s.velocity.z)};
            in.heading=track_angle(float(1-2*(q.y*q.y+q.z*q.z)),float(2*(q.x*q.y+q.w*q.z)));
            in.forward_speed=float(forward_speed(s));in.absolute_speed=float(speed(s));
            in.category=p.driver_category;in.route_distance=p.route_distance;
            in.rear_drive_fraction=spec_.handling.rear_drive_fraction;
            for(unsigned k=0;k<4;++k){in.contact[k]=s.wheels[k].contact;in.wheel_slip[k]=s.wheels[k].prepared_slip;}
            if(!ai_driver_drag_step(in,world_dt,drag_driver_)) {error="drag human steering refused";return false;}
            //4603D0: real pedals/shift/nitrous, AI road steering. Do not pass
            //the lane button through ordinary player steering a second time.
            held.road_steering=true;held.road_angle_word_valid=true;held.road_angle_word=drag_driver_.steering.word;
            held.road_angle=double(static_cast<std::int16_t>(held.road_angle_word))*(6.28318530717958647692/65536);
            held.road_target=held.road_angle;
        }
        if(finished_human) {
            //4352F3 changes the human controller to3; action17/4537D0
            //publishes an enabled path at desired speed0. Keep the original
            //car physics and let the same ordinary driver brake it to rest.
            const auto& p=plans->human;const auto& c=car();const auto& q=c.orientation;
            AiDriverForwardInput in;in.path=p.path;in.desired_speed=0;in.enabled=true;
            in.position={float(c.position.x),float(c.position.y),float(c.position.z)};
            in.velocity={float(c.velocity.x),float(c.velocity.y),float(c.velocity.z)};
            in.heading=track_angle(float(1-2*(q.y*q.y+q.z*q.z)),float(2*(q.x*q.y+q.w*q.z)));
            in.forward_speed=float(forward_speed(c));in.absolute_speed=float(speed(c));
            in.category=p.driver_category;in.route_distance=p.route_distance;
            in.rear_drive_fraction=spec_.handling.rear_drive_fraction;
            in.race_seconds=float(timing_.now_ticks-timing_.race_start_ticks)*.00025F;
            for(unsigned k=0;k<4;++k){in.contact[k]=c.wheels[k].contact;in.wheel_slip[k]=c.wheels[k].prepared_slip;}
            if(!ai_driver_ordinary_step(in,world_dt,finish_driver_)){error="finish driver refused";return false;}
            held={};held.race_active=true;held.throttle=finish_driver_.pedals.throttle;held.brake=finish_driver_.pedals.brake;
            held.road_steering=true;held.road_angle_word_valid=true;held.road_angle_word=finish_driver_.steering.word;
            held.road_angle=double(static_cast<std::int16_t>(held.road_angle_word))*(6.28318530717958647692/65536);
            held.road_target=held.road_angle;first.shift=VehicleShift::none;
        }
        if(first.shift!=VehicleShift::none) held.shift=first.shift;
        held.world_frame_seconds=world_dt;held.world_clock_valid=true;held.world_clock=timing_.now_ticks;
        if(ai_physics_) {
            if(!vehicle_.step_ai(held,track_telemetry_.back(),ai_gear_request)) {
                started_=false;error=vehicle_.error();return false;
            }
            const unsigned ai_slices=unsigned(world_dt/.00839999970048666F)+1;
            contact_component_interval_=world_dt/ai_slices;
            work_.physics_steps+=ai_slices;
            for(std::size_t k=0;k<kDriveStepsPerFrame;++k) {
                inputs_[k]=held;track_telemetry_[k]=track_telemetry_.back();
            }
            for(std::size_t w=0;w<kVehicleWheelCount;++w) {
                double phase=animation_.wheel_phase[w]+car().wheels[w].spin*frame_seconds_;
                while(phase>3.14159265358979323846)phase-=6.28318530717958647692;
                while(phase< -3.14159265358979323846)phase+=6.28318530717958647692;
                animation_.wheel_phase[w]=phase;
            }
        } else for (std::size_t k = 0; k < solver_slices; ++k) {
            inputs_[k]=held;
            if(k) inputs_[k].shift=VehicleShift::none;
            const int previous_gear = car().gear;
            ++work_.physics_steps;
            if (!vehicle_.step(inputs_[k], track_telemetry_[k],
                k+1==solver_slices?frame_seconds_:0.0,
                solver_dt,k==0,solver_slices,true)) {
                started_ = false; error = vehicle_.error(); return false;
            }
            if(drag_ && timing_.stage==RaceStage::racing && previous_gear>=kVehicleGearFirst && car().gear>previous_gear) {
                ++drag_shifts_;
                if(car().transmission.shift_quality>=.85F)++drag_perfect_shifts_;
            }
            if (track_telemetry_[k].reset!=VehicleTrackReset::none) {turbo_={};crash_camera_.reset();}
            if (recovered) {
                if (track_telemetry_[k].reset != VehicleTrackReset::none) {
                    steering_state_={}; steering_result_={}; shifted_=false;
                } else if (previous_gear != car().gear) {
                    shift_step_ = steering_steps_;shift_seconds_=steering_seconds_; shifted_=true;
                }
            }
            for (std::size_t w = 0; w < kVehicleWheelCount; ++w) {
                double phase = animation_.wheel_phase[w] +
                    car().wheels[w].spin * solver_dt;
                while (phase > 3.14159265358979323846)
                    phase -= 6.28318530717958647692;
                while (phase < -3.14159265358979323846)
                    phase += 6.28318530717958647692;
                animation_.wheel_phase[w] = phase;
            }
        }
        if(!ai_physics_)for(unsigned k=solver_slices;k<kDriveStepsPerFrame;++k){inputs_[k]=inputs_[solver_slices-1];track_telemetry_[k]=track_telemetry_[solver_slices-1];}
        timing_input.events = vehicle_.events().data();
        timing_input.event_count = vehicle_.events().size();
        timing_input.speed = static_cast<float>(speed(car()));
        const float induction=*std::max_element(spec_.induction.begin(),spec_.induction.end());
        const auto& engine=car().engine;
        turbo_.update(induction,engine.angular_speed*9.54930400848388671875F,
            spec_.handling.idle_rpm,spec_.handling.max_rpm,
            engine.cutoff_until==0?engine.throttle_multiplier*float(held.throttle):0,
            engine.cutoff_until!=0,nitrous_clock_);
    } else {
        // Body and wheels stay fixed; the human engine still receives gas.
        // AI pre-race rev policy is separate and remains unchanged here.
        const double raw_throttle=direct?direct->throttle:
            (frame.throttle_from_button?frame.throttle_digital:frame.throttle_analog);
        const double throttle=direct?raw_throttle:drive_neutral_throttle(float(raw_throttle),
            float(inputs_[kDriveStepsPerFrame-1].throttle),float(car().engine_rpm),spec_.handling.idle_rpm,
            world_dt);
        // Consume countdown shift edges without carrying them through GO.
        if(!direct) controls_.step(0,kVehicleGearNeutral,VehicleGearbox::manual);
        for (std::size_t k = 0; k < kDriveStepsPerFrame; ++k) {
            inputs_[k] = {};
            if(!ai_physics_) {
                inputs_[k].throttle=throttle;inputs_[k].handbrake=true;
                if(!vehicle_.step_grid_engine(throttle,track_telemetry_[k],
                    k+1==kDriveStepsPerFrame?kDriveFrameSeconds:0.0,kDriveSolverSeconds,drag_,timing_.now_ticks)) {
                    started_=false;error="grid engine update refused";return false;
                }
            } else {
                track_telemetry_[k].position = car().position;
                track_telemetry_[k].orientation = car().orientation;
            }
        }
    }
        return true;
    };
    //420310 snapshots the registered body list before any integration.
    //468680 orders that list by upper X and then by body registration ID.
    constexpr auto no_prop=std::numeric_limits<std::size_t>::max();
    struct PhysicalStep {unsigned actor;float upper;std::size_t prop=no_prop;};
    std::array<PhysicalStep,4+kRaceTrafficCapacity+32> physical{};unsigned physical_count=0;
    physical[physical_count++]={0,float(car().position.x)+contact_bounds_.radius};
    for(unsigned i=0;i<rivals_.size();++i)
        physical[physical_count++]={i+1,float(rivals_[i]->car().position.x)+rivals_[i]->contact_bounds_.radius};
    if(traffic_&&!external_contact_owner_)for(unsigned i=0;i<traffic_->actors().size();++i) {
        const auto& t=traffic_->actors()[i];if(t.active)
            physical[physical_count++]={i+4,float(t.car.position.x)+t.bounds.radius};
    }
    if(props_&&!race_car_held(timing_)&&!external_contact_owner_) {
        const auto begin=profile?timer->now_us():0;
        props_->queued_contacts_=true;props_->clock_=std::uint32_t(timing_.now_ticks);
        props_->maintain_pool(course_.world,car().position,props_->clock_,timing_.step);
        if(!props_->prepare_frame(course_.world,error,world_dt))return false;
        props_->world_frame_ready_=true;
        for(std::size_t i=0;i<props_->objects_.size();++i) {
            const auto& p=props_->objects_[i];
            if(p.body_id>=0&&!p.expired)
                physical[physical_count++]={0,float(p.state.position.x)+p.bounds.radius,i};
        }
        if(profile)profile->prop_motion=(timer->now_us()-begin)/1000.0;
    }
    std::sort(physical.begin(),physical.begin()+physical_count,
        [&](const PhysicalStep& a,const PhysicalStep& b){
            const auto rank=[&](const PhysicalStep& body) {
                return body.prop==no_prop?int(registration_rank(body.actor)):props_->objects_[body.prop].body_id;
            };
            return a.upper!=b.upper?a.upper<b.upper:rank(a)<rank(b);
        });
    if(profile)profile->rivals=profile->player=0;
    for(unsigned i=0;i<physical_count;++i) {
        const auto actor=physical[i].actor;const auto begin=profile?timer->now_us():0;bool okay;
        if(physical[i].prop!=no_prop) {
            auto& p=props_->objects_[physical[i].prop];okay=true;
            if(p.awake) {
                okay=props_->advance_body(p,course_.world,error,world_dt);
                contact_component_interval_=world_dt/float(unsigned(world_dt/.01F)+1);
            }
            if(profile)profile->prop_motion+=(timer->now_us()-begin)/1000.0;
        } else if(actor==0) {
            okay=advance_player();
            if(!held_grid)contact_component_interval_=ai_physics_?world_dt/float(unsigned(world_dt/.00839999970048666F)+1):float(solver_dt);
            if(profile)profile->player+=(timer->now_us()-begin)/1000.0;
        } else if(actor<4) {
            okay=advance_rival(actor-1);
            if(!held_grid)contact_component_interval_=rivals_[actor-1]->contact_component_interval_;
            if(profile)profile->rivals+=(timer->now_us()-begin)/1000.0;
        } else {
            okay=traffic_->advance_body(actor-4,error);
            if(traffic_->actors()[actor-4].impacted)contact_component_interval_=traffic_->last_component_interval;
            if(profile)profile->traffic_motion+=(timer->now_us()-begin)/1000.0;
        }
        if(!okay){if(traffic_&&!external_contact_owner_)traffic_->finish_frame();return false;}
    }
    if(traffic_&&!external_contact_owner_)traffic_->finish_frame();
    if(profile)profile_start=timer->now_us();
    if(!race_car_held(timing_) && !external_contact_owner_) {
        if(!resolve_world_contacts(previous_poses,previous_resets,error,profile,timer))return false;
    }
    if(!external_contact_owner_) {
        //426930 runs once after the shared contact branch, with the saved
        //pre-world velocity. Nested rival advances do not publish early.
        const auto publish_acceleration=[&](RaceSession& actor,unsigned i) {
            const bool reset=actor.telemetry().resets!=previous_resets[i];
            actor.vehicle_.publish_world_acceleration(reset?actor.car().velocity:
                previous_poses[i].velocity,world_dt);
        };
        publish_acceleration(*this,0);
        for(std::size_t i=0;i<rivals_.size();++i)publish_acceleration(*rivals_[i],unsigned(i+1));
        //426A73..426A8B: actor3B8 accumulates published absolute speed*dt.
        //The owning world publishes once after the contact queue, not again
        //for every rewind/continuation or the nested rival advance.
        const auto publish_distance=[&](RaceSession& actor) {
            const float magnitude=actor.car().owner_speeds.magnitude;
            if(magnitude>=0&&magnitude<=1000)
                actor.travelled_distance_=world_dt*magnitude+actor.travelled_distance_;
        };
        publish_distance(*this);
        for(auto& rival:rivals_)publish_distance(*rival);
        //420E51 updates animation movers after contact/426930 publication;
        //4281E0 then admits train release for the NEXT world frame.
        if(!advance_world_motion(world_dt,plans&&plans->drag_steering?plans->human.driver_category:-1,error)){
            started_=false;return false;
        }
    }
    stage(&RaceSimulationProfile::contacts);
    timing_events_.clear();
    if(!finish_probe_events_.empty())
        timing_inputs[0]={finish_probe_events_.data(),finish_probe_events_.size(),55.F};
    auto frame_timing=timing_config_;frame_timing.ticks_per_step=std::int32_t(world_dt*4000.F+.5F);
    if(lap_knockout_)update_standings();
    if (!step_race_timing(course_.route, frame_timing, timing_, timing_inputs.data(),
                          timing_events_, error,lap_knockout_?standings_.order.data():nullptr)) { started_ = false; return false; }
    finish_probe_events_.clear();
    if(held_grid && !race_car_held(timing_)) {
        vehicle_.release_grid_engine(drag_,drag_category_,drag_shift_scale_);
        vehicle_.begin_race();
    }
    update_standings();
    if(drag_&&timing_.go_valid) {
        //435570: totalled reason7 takes precedence over engine reason6.
        // Both receive the last unoccupied place, without a timed finish.
        for(unsigned i=0;i<timing_.racers.size();++i) {
            auto& row=timing_.racers[i];
            const auto& failed_car=i?rival(i-1).car():car();
            if((!failed_car.drag_totalled&&!failed_car.engine.blown)||row.status!=RacerStatus::not_finished)continue;
            int place=int(timing_.racers.size());
            for(;place>0;--place) {
                bool occupied=false;
                for(const auto& r:timing_.racers)occupied=occupied||r.position==place;
                if(!occupied)break;
            }
            row.status=failed_car.drag_totalled?RacerStatus::totalled:RacerStatus::blown_engine;row.position=place;
            row.finish_valid=false;row.finish_step=timing_.step;
        }
        if(timing_.racers[0].status!=RacerStatus::not_finished)timing_.outcome=RaceOutcome::finished;
    }
    if ((race_car_held(timing_)&&timing_.stage!=RaceStage::countdown) ||
        timing_.outcome!=RaceOutcome::running) {
        nitrous_resource_.stop(nitrous_clock_);nitrous_engine_.active=false;
    }
    update_style();
    if (presentation_==RacePresentation::player) {
      if (controls_.frame_state().camera) chase_.next_pov(spec_.drift_mode);
      ++work_.camera_updates;
      VehicleVec3 acceleration{};
      const bool reset=telemetry().resets!=previous_resets[0];
      if(!reset) {
          const auto& a=car().owner_motion.acceleration;
          acceleration={a[0],a[1],a[2]};
      }
      const auto subject=chase_subject(car(),spec_,acceleration);
      if (!(reset?chase_.reset(subject,camera_world_,view_):
            chase_.update(subject,frame_seconds_,camera_world_,view_,controls_.frame_state().look_back))) {
        started_ = false; error = chase_.error(); return false;
      }
    }
    visual_frames_+=frame_seconds_/kDriveFrameSeconds;
    if(crash_source_) {
        const bool allowed=timing_.stage==RaceStage::racing&&timing_.outcome==RaceOutcome::running&&!spec_.drift_mode;
        // 42C2FB extends the finish hold when a roadside shot is active.
        // Keep that shot until its own return conditions, without starting
        // another shot after the result has been recorded.
        const bool finishing=timing_.outcome==RaceOutcome::finished&&crash_camera_.event()!=0;
        ChaseCameraView cinematic;
        if(crash_camera_.update(timing_.step_ticks,timing_.step-1,allowed,car(),spec_.handling,float(animation_.wheel_phase[0]),
            progress().coordinate.fraction*100.F,*crash_source_,camera_world_,view_,cinematic,finishing))view_=cinematic;
    }
    if(presentation_==RacePresentation::player) {
        const bool rear=controls_.frame_state().look_back;
        const unsigned event=crash_camera_.event();
        const bool cut=event!=presented_crash_||view_.pov!=presented_pov_||(event==0&&rear!=presented_look_back_)||
            telemetry().resets!=previous_resets[0];
        //47D070 receives73AD38, not scaled73457C. Keep simulation and view
        //motion clocks distinct during the roadside slow-motion sequence.
        camera_motion_.update({{float(view_.eye.x),float(view_.eye.y),float(view_.eye.z)}},float(kDriveFrameSeconds),cut);
        presented_crash_=event;presented_pov_=view_.pov;presented_look_back_=rear;
    }
    record_dirty_=true;
    stage(&RaceSimulationProfile::tail);
    return true;
}

bool RaceSession::resolve_world_contacts(const std::array<RaceContactPose,4>& previous_poses,
    const std::array<std::uint64_t,4>& previous_resets,std::string& error,
    RaceSimulationProfile* profile,TimingService* timer,std::vector<RaceQueuedContact>* trace) {
    constexpr auto none=std::numeric_limits<std::size_t>::max();
    const auto tick=std::uint32_t(timing_.now_ticks);
    prop_contacts_={};
    if(profile)profile->prop_contacts=0;
    if(props_) {
        props_->queued_contacts_=true;props_->clock_=tick;
        // 420310 advances active prop bodies before 420380 searches for contacts.
        const auto begin=profile?timer->now_us():0;
        if(!props_->world_frame_ready_) {
            props_->maintain_pool(course_.world,car().position,tick,timing_.step);
            if(!props_->advance(course_.world,error,float(frame_seconds_))){started_=false;return false;}
        }
        props_->world_frame_ready_=false;
        if(profile)profile->prop_motion+=(timer->now_us()-begin)/1000.0;
    }
    RacePairFrame contacts;contacts.step=timing_.step;std::uint32_t audio_order=0;
    std::array<RaceSession*,4> actors{{this}};
    for(std::size_t i=0;i<rivals_.size();++i)actors[i+1]=rivals_[i].get();
    struct Body {
        const VehicleState* state=nullptr;const VehicleSpec* spec=nullptr;
        const RaceContactBox* box=nullptr;RaceContactPose history;
        RaceTrackBodyBounds bounds;
        RaceProp* prop=nullptr;
        const VehicleWorldObject* object=nullptr;
    };
    const unsigned actor_limit=4U+std::max(6U,traffic_?unsigned(traffic_->actors().size()):0U);
    const unsigned object_begin=actor_limit+32;
    const unsigned object_count=unsigned(world_objects_.size());
    const unsigned dynamic_id_begin=actor_limit+object_count;
    const unsigned body_limit=object_begin+object_count;
    std::array<Body,4+kRaceTrafficCapacity+32+kRaceWorldObjectCapacity> bodies{};
    std::array<bool,4+kRaceTrafficCapacity+32+kRaceWorldObjectCapacity> reset{};
    for(unsigned i=0;i<=rivals_.size();++i) {
        reset[i]=actors[i]->telemetry().resets!=previous_resets[i];
        bodies[i]={&actors[i]->car(),&actors[i]->model_.spec(),&actors[i]->contact_box_,
            reset[i]?race_contact_pose(actors[i]->car()):previous_poses[i],
            actors[i]->contact_bounds_};
    }
    if(traffic_)for(unsigned i=0;i<traffic_->contact_actors().size();++i) {
        auto& t=traffic_->contact_actors()[i];if(!t.active)continue;
        reset[4+i]=contact_traffic_epochs_[i]!=t.activations;
        contact_traffic_epochs_[i]=t.activations;
        bodies[4+i]={&t.car,&t.setup.spec,&t.box,t.previous_valid?t.previous_pose:race_contact_pose(t.car),
            t.bounds};
    }
    if(props_)for(auto& p:props_->objects_)if(p.body_id>=0&&!p.expired) {
        if(p.world_slot>=32){error="registered prop has no world slot";return false;}
        bodies[actor_limit+p.world_slot]={&p.state,&p.spec,&p.box,p.history,p.bounds,&p};
    }
    for(unsigned i=0;i<object_count;++i) {
        auto& object=world_objects_[i];if(!object.enabled)continue;
        object.state.simulation_seconds=car().simulation_seconds;
        auto history=race_contact_pose(object.state);history.time-=frame_seconds_;
        bodies[object_begin+i]={&object.state,&object.spec,&object.box,history,object.bounds,nullptr,&course_.world_objects[i]};
    }
    const auto update_cache=[&](unsigned a) {
        auto& body=bodies[a];
        if(body.object)return true;
        if(body.prop) {
            const auto& p=*body.prop;const auto& v=p.state.velocity;
            const float speed=float(vehicle_sqrt((v.x*v.x+v.y*v.y)+v.z*v.z));
            const float radius=std::min(p.bounds.below,p.bounds.above)+std::min(speed*.1F,6.F);
            return update_race_track_polygon_cache(course_.world,race_contact_pose(p.state),
                body.prop->track_cache,radius);
        }
        if(reset[a])world_track_caches_[a]={};
        //426859 prepares the actor cache from the published pre-step pose.
        //Continuation reuses this cache; it does not reorder all world cells.
        return update_race_track_polygon_cache(course_.world,body.history,world_track_caches_[a]);
    };
    for(unsigned a=0;a<body_limit;++a) {
        if(!bodies[a].state){if(a<actor_limit)world_track_caches_[a]={};continue;}
        if(!update_cache(a)) {
            error="world polygon cache refused";started_=false;return false;
        }
    }
    const auto near=[&](unsigned a,unsigned b) {
        const auto& x=*bodies[a].state;const auto& y=*bodies[b].state;
        const float r=bodies[a].bounds.radius+bodies[b].bounds.radius;
        return std::abs(float(x.position.x)-float(y.position.x))<=r&&
            std::abs(float(x.position.y)-float(y.position.y))<=r;
    };
    const auto polygon_data=[&](std::size_t index,std::array<ContactVector,4>& corners,ContactVector& normal) {
        const auto& p=course_.world.collision.polygons[index];
        for(unsigned i=0;i<4;++i)std::copy_n(p.corner[i],3,corners[i].begin());
        normal=race_track_polygon_normal(corners);
    };
    const auto track_near=[&](const WorldContactPair& pair) {
        const auto& body=bodies[pair.a];const auto& s=*body.state;
        const auto& p=course_.world.collision.polygons[pair.polygon];
        float low[2]={p.corner[0][0],p.corner[0][1]},high[2]={low[0],low[1]};
        for(unsigned i=1;i<4;++i)for(unsigned k=0;k<2;++k) {
            low[k]=std::min(low[k],p.corner[i][k]);high[k]=std::max(high[k],p.corner[i][k]);
        }
        const float r=body.bounds.radius;
        return !(float(s.position.x)-r>high[0]||float(s.position.x)+r<low[0]||
            float(s.position.y)-r>high[1]||float(s.position.y)+r<low[1]);
    };
    const auto prop_near=[&](unsigned a,const RaceProp& p,const std::array<float,4>& box) {
        const auto& body=bodies[a];const auto& s=*body.state;
        const float x=float(s.position.x),y=float(s.position.y),r=body.bounds.radius;
        return !p.expired&&!p.proxy_discarded&&x-r<=box[2]&&x+r>=box[0]&&y-r<=box[3]&&y+r>=box[1];
    };
    const auto acquire_body_id=[&]() {
        const auto free=std::find(world_shape_ids_.begin(),world_shape_ids_.end(),false);
        const auto id=unsigned(free-world_shape_ids_.begin());
        if(free==world_shape_ids_.end())world_shape_ids_.push_back(true);
        else *free=true;
        return dynamic_id_begin+id;
    };
    world_contact_pairs_.erase(std::remove_if(world_contact_pairs_.begin(),world_contact_pairs_.end(),
        [&](const WorldContactPair& p) {
            if(!bodies[p.a].state||reset[p.a]||(!p.track()&&(!bodies[p.b].state||reset[p.b])))return true;
            return !p.record.active&&!(p.track()?track_near(p):near(p.a,p.b));
        }),world_contact_pairs_.end());
    world_track_shapes_.erase(std::remove_if(world_track_shapes_.begin(),world_track_shapes_.end(),
        [&](const WorldTrackShape& shape) {
            //466EEF..466F33 destroys a kind3 shape when its last pair retires.
            if(std::any_of(world_contact_pairs_.begin(),world_contact_pairs_.end(),
                [&](const WorldContactPair& pair){return pair.track()&&pair.polygon==shape.polygon;}))return false;
            world_shape_ids_[shape.id-dynamic_id_begin]=false;
            return true;
        }),world_track_shapes_.end());
    std::size_t registered_props=0;
    if(props_)for(auto& prop:props_->objects_)if(prop.body_id>=0) {
        const auto index=std::size_t(&prop-props_->objects_.data());
        const bool unused=!prop.moved&&prop_clock_age(tick,prop.registered_tick)>2.F&&
            std::none_of(world_contact_pairs_.begin(),world_contact_pairs_.end(),
                [index](const WorldContactPair& pair){return pair.prop==index||pair.prop_a==index;});
        if(prop.expired||unused) {
            world_shape_ids_[unsigned(prop.body_id)-dynamic_id_begin]=false;
            if(prop.world_slot<32)bodies[actor_limit+prop.world_slot]={};
            prop.world_slot=32;prop.track_cache={};
            prop.body_id=prop.proxy_id;
            if(unused)props_->restore_proxy(prop);
        } else {
            if(registered_props==32){error="invalid registered prop count";return false;}
            ++registered_props;
        }
    }
    // Retained records protect pairs from broad-phase retirement (420380).
    const auto upper_x=[&](unsigned a) {return float(bodies[a].state->position.x)+bodies[a].bounds.radius;};
    const auto body_rank=[&](unsigned a){return bodies[a].object?int(a-object_begin):
        bodies[a].prop?bodies[a].prop->body_id:int(object_count+registration_rank(a));};
    const auto append_pairs=[&](unsigned affected_a,unsigned affected_b) {
        std::vector<unsigned> scan;
        if(affected_a==body_limit) {
            for(unsigned a=0;a<body_limit;++a)if(bodies[a].state)scan.push_back(a);
            //468680 maintains the registered list by body+2C (upper X).
            //420487 traverses that list; canonical actor indices change the
            //ordered response callbacks and hence the resulting impulse.
            std::sort(scan.begin(),scan.end(),[&](unsigned a,unsigned b){
                const float ax=upper_x(a),bx=upper_x(b);
                return ax!=bx?ax<bx:body_rank(a)<body_rank(b);
            });
        } else {
            scan.push_back(affected_a);if(affected_b<body_limit)scan.push_back(affected_b);
        }
        std::vector<std::size_t> candidates;
        struct Other {unsigned body;std::size_t polygon,prop;float upper;int id;};
        for(std::size_t cursor=0;cursor<scan.size();++cursor) {
            const unsigned a=scan[cursor];
            const auto& body=bodies[a];
            if(body.prop&&body.prop->expired)continue;
            std::vector<Other> others;
            {
                if(!body.object) {
                std::vector<std::size_t> registered;registered.reserve(world_track_shapes_.size());
                for(const auto& shape:world_track_shapes_)registered.push_back(shape.polygon);
                const auto& cache=body.prop?body.prop->track_cache:world_track_caches_[a];
                if(!race_track_candidates(course_.world,*body.state,body.bounds,cache,registered,candidates))return false;
                for(auto polygon:candidates)
                    if(std::none_of(world_track_shapes_.begin(),world_track_shapes_.end(),
                        [&](const WorldTrackShape& shape){return shape.polygon==polygon;}))
                        world_track_shapes_.push_back({polygon,acquire_body_id(),true});
                //469810 creates shapes;469580 considers all registered
                //overlapping bodies, including polygons created by other cars.
                for(const auto& shape:world_track_shapes_) {
                    WorldContactPair pair;pair.a=a;pair.polygon=shape.polygon;
                    if(!track_near(pair))continue;
                    const auto& polygon=course_.world.collision.polygons[shape.polygon];
                    float high=polygon.corner[0][0];
                    for(unsigned i=1;i<4;++i)high=std::max(high,polygon.corner[i][0]);
                    others.push_back({body_limit,shape.polygon,none,high,int(shape.id)});
                }
                }
                for(unsigned b=0;b<body_limit;++b)if(a!=b&&bodies[b].state&&
                    !(body.object&&bodies[b].object)&&
                    (!bodies[b].prop||!bodies[b].prop->expired)&&near(a,b))
                    others.push_back({b,none,none,upper_x(b),body_rank(b)});
            }
            if(props_&&(a>=4||(!actor_ghost(a)&&!actor_recovering(a)))) {
                const auto add_prop=[&](std::size_t i) {
                    const auto& p=props_->objects_[i];const auto& box=p.placement_bounds;
                    if(prop_near(a,p,box))others.push_back({body_limit,none,i,box[2],p.body_id});
                };
                const float x=float(body.state->position.x),r=body.bounds.radius;
                const auto min_x=[&](std::size_t i){return props_->objects_[i].placement_bounds[0];};
                const auto& order=props_->proxy_order_;
                const auto first=std::lower_bound(order.begin(),order.end(),x-r-props_->widest_proxy_,
                    [&](std::size_t i,float lo){return min_x(i)<lo;});
                for(auto it=first;it!=order.end()&&min_x(*it)<=x+r;++it)
                    if(props_->objects_[*it].body_id<0)add_prop(*it);
            }
            std::sort(others.begin(),others.end(),[](const Other& x,const Other& y){
                return x.upper!=y.upper?x.upper<y.upper:x.id<y.id;
            });
            for(auto other:others) {
                if(other.prop!=none) {
                    auto& p=props_->objects_[other.prop];
                    if(p.body_id<0) {
                        if(registered_props==32){
                            //469714 converts then destroys the Virgin even if
                            //the live pool is full. Its scenery must not remain
                            //visible after losing its collision proxy.
                            p.proxy_discarded=true;p.expired=true;continue;
                        }
                        unsigned slot=0;while(slot<32&&bodies[actor_limit+slot].state)++slot;
                        if(slot==32)return false;
                        ++registered_props;p.world_slot=slot;
                        props_->convert_proxy(p,course_.world);
                        p.body_id=int(acquire_body_id());p.registered_tick=tick;
                        p.history=race_contact_pose(p.state);
                        p.state.simulation_seconds+=float(frame_seconds_);
                        bodies[actor_limit+slot]={&p.state,&p.spec,&p.box,p.history,p.bounds,&p};
                        if(!update_cache(actor_limit+slot))return false;
                        scan.push_back(actor_limit+slot);
                    }
                    other.body=actor_limit+p.world_slot;
                }
                const bool found=std::any_of(world_contact_pairs_.begin(),world_contact_pairs_.end(),
                    [&](const WorldContactPair& p) {
                        if(other.polygon!=none)return p.a==a&&p.polygon==other.polygon;
                        return !p.track()&&((p.a==a&&p.b==other.body)||(p.b==a&&p.a==other.body));
                    });
                if(!found) {
                    WorldContactPair pair;pair.a=a;pair.b=other.body;pair.polygon=other.polygon;
                    pair.object=body.object||(!pair.track()&&bodies[pair.b].object);
                    if(!pair.track()&&!pair.object&&pair.a>=actor_limit&&pair.b<actor_limit)std::swap(pair.a,pair.b);
                    if(bodies[pair.a].prop)pair.prop_a=std::size_t(bodies[pair.a].prop-props_->objects_.data());
                    if(!pair.track()&&bodies[pair.b].prop)pair.prop=std::size_t(bodies[pair.b].prop-props_->objects_.data());
                    world_contact_pairs_.push_back(pair);
                }
            }
        }
        return true;
    };
    if(!append_pairs(body_limit,body_limit)){error="world contact candidates refused";started_=false;return false;}
    struct Job {
        RaceContactJob car;RaceTrackContactJob track;
        unsigned serial=0,repetitions=0;bool valid=false;
        float remaining=0;
    };
    std::vector<Job> jobs(world_contact_pairs_.size());unsigned serial=0;
    const auto enqueue=[&](unsigned k) {
        auto& pair=world_contact_pairs_[k];const auto& a=bodies[pair.a];auto& j=jobs[k];j.valid=false;
        if(j.repetitions>=2)return true;
        if(a.prop&&a.prop->expired)return true;
        ++work_.pair_candidates;
        if(pair.track()) {
            std::array<ContactVector,4> corners;ContactVector normal;polygon_data(pair.polygon,corners,normal);
            if(a.prop&&(course_.world.collision.polygons[pair.polygon].unknown0[3]&2)) {
                const bool triangle=corners[2]==corners[3];const unsigned count=triangle?3:4;
                float centre=0;for(unsigned i=0;i<count;++i)centre+=corners[i][2];
                centre=centre/float(count)-normal[2]*.5F;
                const float shift=float(a.state->position.z)-centre;
                for(auto& corner:corners)corner[2]+=shift;
            }
            const auto reference=race_track_side_reference(a.bounds,a.history);
            auto shape=std::find_if(world_track_shapes_.begin(),world_track_shapes_.end(),
                [&](const WorldTrackShape& value){return value.polygon==pair.polygon;});
            if(shape==world_track_shapes_.end())return false;
            //473619 constructs an expanded shape.468160 clears it after the
            //first admission query, even when no contact job is admitted.
            const bool expanded=shape->expanded;shape->expanded=false;
            if(!search_race_track_contact(*a.state,*a.spec,*a.box,a.history,corners,normal,reference,j.track,&pair.query,
                expanded,a.prop!=nullptr,a.prop&&a.prop->impact.activation>0))return false;
            j.valid=j.track.admitted;j.remaining=j.track.remaining;
        } else {
            //467F17/468175: a ghost suppresses response only when both
            //bodies are dynamic. Static track shapes remain solid.
            const auto& b=bodies[pair.b];
            if(b.prop&&b.prop->expired)return true;
            if(!pair.object&&((pair.a<4&&(actor_ghost(pair.a)||actor_recovering(pair.a)))||
               (pair.b<4&&(actor_ghost(pair.b)||actor_recovering(pair.b))))) {
                // 468060 tests the narrow overlap before the ghost response
                // gate. Its flag15 stamps body148 for the next owner update.
                RaceHullQuery overlap;
                if(!query_race_hulls(*a.state,*a.spec,*a.box,*b.state,*b.spec,*b.box,overlap,&pair.query))return false;
                if(overlap.distance<=double(.0001F)) {
                    if(pair.a<4)actors[pair.a]->vehicle_.note_ghost_overlap(tick);
                    if(pair.b<4)actors[pair.b]->vehicle_.note_ghost_overlap(tick);
                }
                return true;
            }
            const auto kind=[](const Body& body) {
                return body.object?RaceContactPartner::fixed_world:!body.prop?RaceContactPartner::car:body.prop->impact.activation>0?
                    RaceContactPartner::pinned_prop:RaceContactPartner::prop;
            };
            if(pair.scenery())++props_->candidates;
            if(!search_race_contact(*a.state,*a.spec,*a.box,*b.state,*b.spec,*b.box,
                a.history,b.history,j.car,&pair.query,kind(b),kind(a)))return false;
            j.valid=j.car.admitted;j.remaining=j.car.remaining;
        }
        j.serial=serial++;return true;
    };
    for(unsigned k=0;k<jobs.size();++k)if(!enqueue(k)){error="world contact query refused";started_=false;return false;}
    // 420380 shares one response budget across car, polygon and prop jobs.
    for(unsigned processed=0;processed<20;++processed) {
        unsigned best=unsigned(jobs.size());
        for(unsigned k=0;k<jobs.size();++k)if(jobs[k].valid)
            if(best==jobs.size()||jobs[k].remaining>jobs[best].remaining||
                (jobs[k].remaining==jobs[best].remaining&&jobs[k].serial<jobs[best].serial))best=k;
        if(best==jobs.size())break;
        const auto job=jobs[best];jobs[best].valid=false;++jobs[best].repetitions;
        auto& pair=world_contact_pairs_[best];const unsigned a=pair.a,b=pair.track()?body_limit:pair.b;
        const auto prop_index=pair.prop;
        //Original46B5E0 activates both components before rewinding either.
        if(traffic_)for(unsigned id:{a,b})if(id>=4&&id<actor_limit) {
            bool activated=false;
            if(!traffic_->prepare_contact(id-4,error,activated)){started_=false;return false;}
            if(activated)contact_component_interval_=0;
        }
        const auto published_a=*bodies[a].state;
        RaceContactContext context{pair.record,contact_seed_,tick};RacePairContact contact;bool ok=false;
        context.drag=drag_;
        context.physical_selectors={{a<4?(actors[a]->ai_physics_?3U:1U):3U,
            b<4?(actors[b]->ai_physics_?3U:1U):3U}};
        struct RefreshOwner {
            VehicleTrackSession* session[2];VehicleState published[2];
            RaceTraffic* traffic;unsigned ids[2];std::string* error;float interval;
            RaceProps* props;RaceProp* prop[2];
            ContactRecord* record;std::size_t polygon;
            RaceSession* world;
            bool fixed_world[2]{};
        };
        RefreshOwner refresh{{a<4?&actors[a]->vehicle_:nullptr,b<4?&actors[b]->vehicle_:nullptr},
            {published_a,b<body_limit?*bodies[b].state:VehicleState{}},traffic_.get(),{a,b},&error,contact_component_interval_,
            props_.get(),{bodies[a].prop,b<body_limit?bodies[b].prop:nullptr},&pair.record,pair.polygon,this};
        refresh.fixed_world[0]=bodies[a].object!=nullptr;
        refresh.fixed_world[1]=b<body_limit&&bodies[b].object;
        for(unsigned k=0;k<2;++k) {
            const auto index=k?b:a;
            if(index<body_limit&&bodies[index].object) {
                context.body_names[k]=bodies[index].object->type;
                context.physical_selectors[k]=0;
            }
        }
        context.refresh_owner=&refresh;
        context.refresh=[](void* opaque,unsigned slot,VehicleState& sampled) {
            auto& owner=*static_cast<RefreshOwner*>(opaque);
            if(owner.fixed_world[slot])return true;
            if(owner.session[slot])return owner.session[slot]->refresh_contact(sampled,&owner.published[slot],owner.interval);
            if(owner.prop[slot]) {
                auto& prop=*owner.prop[slot];
                prop.contact_count=0;
                for(const auto& p:owner.world->world_contact_pairs_)if(p.record.active&&
                    (p.a==owner.ids[slot]||(!p.track()&&p.b==owner.ids[slot]))) {
                    if(prop.contact_count==0)prop.first_contact_tick=p.record.creation_tick;
                    ++prop.contact_count;
                }
                if(owner.polygon!=std::numeric_limits<std::size_t>::max()&&owner.record->active) {
                    const auto polygon=std::uint32_t(owner.polygon);
                    prop_touch_ground(prop.ground_contacts,polygon,owner.record->creation_tick);
                    for(auto& ground:prop.ground_contacts)if(ground.polygon==polygon)
                        ground.touched=owner.record->last_hit_tick;
                    prop.terrain_contact=true;
                }
                return owner.props->refresh_contact(prop,sampled);
            }
            return owner.ids[slot]>=4&&owner.traffic&&owner.ids[slot]-4<owner.traffic->actors().size()&&owner.traffic->refresh_contact(
                owner.ids[slot]-4,sampled,*owner.error,&owner.published[slot],owner.interval);
        };
        if(pair.track()) {
            const auto flags=course_.world.collision.polygons[pair.polygon].unknown0[3];
            const float friction=(flags&0xAU)?0.3F:0.6F;
            if(bodies[a].prop) {
                auto& p=*bodies[a].prop;
                ok=resolve_swept_race_prop_track(p.state,p.spec,p.impact,bodies[a].history,job.track,
                    friction,context,contact)&&props_->continue_contact(p,course_.world,error,contact.remaining_a);
            } else if(a<4)ok=actors[a]->vehicle_.meet_track(bodies[a].history,job.track,friction,context,contact);
            else {
                auto& t=traffic_->contact_actors()[a-4];t.impacted=true;
                ok=resolve_swept_race_track_contact(t.car,t.setup.spec,bodies[a].history,job.track,friction,context,contact)&&
                    traffic_->continue_contact(a-4,contact.remaining_a,error,&published_a,true);
            }
            if(ok&&a<4) {
                auto& t=actors[a]->track_telemetry_.back();++t.contacts;
                if(contact.applied)++t.impulses;
                if(t.audio_contact_count<t.audio_contacts.size()) {
                    auto& audio=t.audio_contacts[t.audio_contact_count++];audio.polygon=std::uint32_t(pair.polygon);audio.audio_order=++audio_order;
                    const auto basis=race_contact_axes(published_a);
                    audio.forward={basis[0][0],basis[0][1],basis[0][2]};audio.up={basis[2][0],basis[2][1],basis[2][2]};
                    audio.normal={-job.track.geometry.normal[0],-job.track.geometry.normal[1],-job.track.geometry.normal[2]};
                    audio.impulse={contact.impulse[0],contact.impulse[1],contact.impulse[2]};audio.slide_mps=context.record.tangent_speed;
                    audio.point={job.track.geometry.point[0],job.track.geometry.point[1],job.track.geometry.point[2]};
                }
            }
        } else if(pair.object) {
            const unsigned fixed=bodies[a].object?a:b,moving=fixed==a?b:a;
            auto& object=world_objects_[fixed-object_begin];
            const std::array<bool,2> fixed_world{{fixed==a,fixed==b}};
            if(moving<4) {
                ok=actors[moving]->vehicle_.meet_external(object.state,object.spec,*bodies[moving].box,object.box,
                    contact,&bodies[moving].history,&bodies[fixed].history,&context,&job.car,moving==b,true);
            } else if(moving<actor_limit) {
                auto& t=traffic_->contact_actors()[moving-4];t.impacted=true;
                auto& state_a=fixed==a?object.state:t.car;auto& state_b=fixed==b?object.state:t.car;
                ok=resolve_swept_race_pair(state_a,*bodies[a].spec,*bodies[a].box,
                    state_b,*bodies[b].spec,*bodies[b].box,contact,bodies[a].history,bodies[b].history,
                    &context,&job.car,fixed_world)&&traffic_->continue_contact(moving-4,
                    moving==a?contact.remaining_a:contact.remaining_b,error,&refresh.published[moving==a?0:1],true);
            } else {
                auto& p=*bodies[moving].prop;
                auto& state_a=fixed==a?object.state:p.state;auto& state_b=fixed==b?object.state:p.state;
                ok=resolve_swept_race_rigid_pair(state_a,*bodies[a].spec,*bodies[a].box,fixed==a?nullptr:&p.impact,
                    state_b,*bodies[b].spec,*bodies[b].box,fixed==b?nullptr:&p.impact,
                    bodies[a].history,bodies[b].history,job.car,context,contact,fixed_world);
                if(ok) {
                    props_->observe_contact(p,contact,nullptr,audio_order,false);
                    ok=props_->continue_contact(p,course_.world,error,moving==a?contact.remaining_a:contact.remaining_b);
                }
            }
            if(ok) {
                bodies[b].history=contact.resume_b;
                if(bodies[b].prop)bodies[b].prop->history=contact.resume_b;
            }
            if(ok&&contact.applied) {
                ++pair_impulses_;
                if(moving==0) {
                    const auto& before=refresh.published[moving==a?0:1];
                    if(crash_source_) {
                        const auto& j=contact.impulse;
                        const float magnitude=std::min(50.F,float(vehicle_sqrt((j[0]*j[0]+j[1]*j[1])+j[2]*j[2])));
                        crash_camera_.contact(magnitude,before.collision_state==0&&car().collision_state==1,tick,
                            timing_.stage==RaceStage::racing&&timing_.outcome==RaceOutcome::running&&!spec_.drift_mode,
                            car(),progress().coordinate.fraction*100.F,*crash_source_,camera_world_);
                    }
                    if(contacts.count<contacts.pairs.size()) {
                        auto& observation=contacts.pairs[contacts.count++];
                        observation={0,0x40000000U|(fixed-object_begin),contact,++audio_order,4};
                        observation.slide_mps=context.record.tangent_speed;
                        if(moving==b) {
                            for(auto& v:observation.contact.impulse)v=-v;
                            std::swap(observation.contact.position_a,observation.contact.position_b);
                            std::swap(observation.contact.velocity_a,observation.contact.velocity_b);
                        }
                        const auto axes=race_contact_axes(before);
                        observation.forward={axes[0][0],axes[0][1],axes[0][2]};
                        observation.up={axes[2][0],axes[2][1],axes[2][2]};
                    } else ++contacts.dropped;
                }
            }
        } else if(pair.scenery()) {
            const auto begin=profile?timer->now_us():0;
            auto& p=*bodies[b].prop;
            if(bodies[a].prop) {
                auto& first=*bodies[a].prop;
                ok=resolve_swept_race_rigid_pair(first.state,first.spec,first.box,&first.impact,
                    p.state,p.spec,p.box,&p.impact,bodies[a].history,bodies[b].history,job.car,context,contact);
                if(ok) {
                    props_->observe_contact(first,contact,nullptr,audio_order,false);
                    ok=props_->continue_contact(first,course_.world,error,contact.remaining_a);
                }
            } else if(a<4)ok=actors[a]->vehicle_.meet_prop(p,*bodies[a].box,bodies[a].history,job.car,context,contact);
            else {
                auto& t=traffic_->contact_actors()[a-4];t.impacted=true;
                ok=resolve_swept_race_prop(t.car,t.setup.spec,t.box,p.state,p.spec,p.box,p.impact,
                    bodies[a].history,p.history,job.car,context,contact)&&
                    traffic_->continue_contact(a-4,contact.remaining_a,error,&published_a,true);
            }
            if(ok) {
                props_->observe_contact(p,contact,a==0?&prop_contacts_:nullptr,audio_order,a<actor_limit);
                p.history=contact.resume_b;
                ok=props_->continue_contact(p,course_.world,error,contact.remaining_b);
                bodies[b].history=contact.resume_b;
            }
            if(profile)profile->prop_contacts+=(timer->now_us()-begin)/1000.0;
        } else {
            const auto published_b=*bodies[b].state;
            if(traffic_)for(unsigned id:{a,b})if(id>=4)traffic_->contact_actors()[id-4].impacted=true;
            if(a<4&&b<4)ok=actors[a]->vehicle_.meet(actors[b]->vehicle_,*bodies[a].box,*bodies[b].box,contact,
                &bodies[a].history,&bodies[b].history,&context,&job.car);
            else if(a<4) {
                auto& t=traffic_->contact_actors()[b-4];
                ok=actors[a]->vehicle_.meet_external(t.car,t.setup.spec,*bodies[a].box,t.box,contact,
                    &bodies[a].history,&bodies[b].history,&context,&job.car);
            } else if(b<4) {
                auto& t=traffic_->contact_actors()[a-4];
                ok=actors[b]->vehicle_.meet_external(t.car,t.setup.spec,*bodies[b].box,t.box,contact,
                    &bodies[b].history,&bodies[a].history,&context,&job.car,true);
            } else {
                auto& ta=traffic_->contact_actors()[a-4];auto& tb=traffic_->contact_actors()[b-4];
                ok=resolve_swept_race_pair(ta.car,ta.setup.spec,ta.box,tb.car,tb.setup.spec,tb.box,
                    contact,bodies[a].history,bodies[b].history,&context,&job.car);
            }
            if(ok&&a>=4)ok=traffic_->continue_contact(a-4,contact.remaining_a,error,&published_a,true);
            if(ok&&b>=4)ok=traffic_->continue_contact(b-4,contact.remaining_b,error,&published_b,true);
            if(ok&&contact.applied) {
                if(crash_source_&&(a==0||b==0)) {
                    const auto& before=a==0?published_a:published_b;
                    const auto& j=contact.impulse;
                    const float magnitude=std::min(50.F,float(vehicle_sqrt((j[0]*j[0]+j[1]*j[1])+j[2]*j[2])));
                    crash_camera_.contact(magnitude,before.collision_state==0&&car().collision_state==1,tick,
                        timing_.stage==RaceStage::racing&&timing_.outcome==RaceOutcome::running&&!spec_.drift_mode,
                        car(),progress().coordinate.fraction*100.F,*crash_source_,camera_world_);
                }
                if(style_.ready()&&!spec_.drift_mode&&timing_.stage==RaceStage::racing&&
                   timing_.outcome==RaceOutcome::running&&
                   ((a==0&&b>=4)||(b==0&&a>=4))&&context.record.magnitude_sum>=5&&
                   double(static_cast<std::int32_t>(tick-context.record.creation_tick))*double(.00025F)<=double(.10000000149011612F)) {
                    style_.trigger(15,std::min(context.record.magnitude_sum/40.F,1.F),timing_.now_ticks);
                    near_miss_.impact(timing_.now_ticks);style_.break_chain(22);
                }
                ++pair_impulses_;
                if(a>=4)RaceTraffic::contact(traffic_->contact_actors()[a-4],b,contact.impulse,timing_.step,&context.record);
                if(b>=4)RaceTraffic::contact(traffic_->contact_actors()[b-4],a,contact.impulse,timing_.step,&context.record);
            }
            if(ok){bodies[b].history=contact.resume_b;
                if(contacts.count<contacts.pairs.size())contacts.pairs[contacts.count++]={a<4?a:50+a-4,b<4?b:50+b-4,contact,++audio_order};
                else ++contacts.dropped;
            }
        }
        if(!ok){if(error.empty())error="queued world contact refused";started_=false;return false;}
        //6F0738 is shared across bodies. The last resumed component leaves
        //its slice interval for the next queued job's two force refreshes.
        const unsigned last=b<body_limit&&!bodies[b].object?b:a;
        const float remaining=float(b<body_limit?contact.remaining_b:contact.remaining_a);
        const bool ai=last>=4||actors[last]->ai_physics_;
        const float maximum=bodies[last].prop?.01F:ai?.00839999970048666F:.004399999976158142F;
        contact_component_interval_=remaining/float(unsigned(remaining/maximum)+1);

        if(trace) {
            RaceQueuedContact observed{a,pair.track()?actor_limit:b,pair.polygon,job.remaining,contact.impulse};
            observed.prop=prop_index==none?pair.prop_a:prop_index;
            observed.point=contact.point;unsigned slot=0;
            for(unsigned id:{a,b})if(id<body_limit) {
                observed.position[slot]=bodies[id].state->position;
                observed.velocity[slot]=bodies[id].state->velocity;
                observed.angular_velocity[slot]=bodies[id].state->angular_velocity;
                observed.axes[slot]=race_contact_axes(*bodies[id].state);++slot;
            }
            trace->push_back(observed);
        }
        bodies[a].history=contact.resume_a;
        if(bodies[a].prop)bodies[a].prop->history=contact.resume_a;
        if(!append_pairs(a,b)){error="world contact candidates refused after response";started_=false;return false;}
        jobs.resize(world_contact_pairs_.size());
        for(unsigned k=0;k<jobs.size();++k) {
            const auto& p=world_contact_pairs_[k];
            //420660 iterates the affected-body list inside the pair loop.
            //A pair touching both actors is queried twice, retaining its cache.
            for(unsigned id:{a,b})if(id<body_limit&&(p.a==id||(!p.track()&&p.b==id)))
                if(!enqueue(k)){error="world contact requery refused";started_=false;return false;}
        }
    }
    //420720 retires untouched records AFTER response processing. An old
    //record refreshed by this frame must keep its accumulated impulse.
    for(auto& p:world_contact_pairs_)if(p.record.active&&
        prop_contact_expired(prop_clock_age(tick,p.record.last_hit_tick)))p.record={};
    std::array<unsigned,4+kRaceTrafficCapacity+32+kRaceWorldObjectCapacity> contact_counts{};
    if(props_)for(auto& p:props_->objects_)if(p.body_id>=0){p.ground_contacts.clear();p.contact_count=0;}
    for(const auto& p:world_contact_pairs_)if(p.record.active) {
        ++contact_counts[p.a];if(!p.track())++contact_counts[p.b];
        for(unsigned id:{p.a,p.track()?body_limit:p.b})if(id<body_limit&&bodies[id].prop) {
            auto& prop=*bodies[id].prop;
            if(prop.contact_count==0)prop.first_contact_tick=p.record.creation_tick;
            ++prop.contact_count;
        }
        if(p.track()&&bodies[p.a].prop) {
            auto& prop=*bodies[p.a].prop;
            prop.ground_contacts.push_back({std::uint32_t(p.polygon),p.record.creation_tick,p.record.last_hit_tick});
        }
    }
    if(props_)for(auto& p:props_->objects_)if(p.body_id>=0)p.terrain_contact=!p.ground_contacts.empty();
    for(unsigned i=0;i<=rivals_.size();++i)
        actors[i]->vehicle_.publish_contact_count(contact_counts[i],tick);
    if(traffic_)for(unsigned i=0;i<traffic_->contact_actors().size();++i) {
        auto& t=traffic_->contact_actors()[i];if(!t.active)continue;
        t.car.collision_body_flag=static_cast<std::int8_t>(contact_counts[4+i]);
        update_car_collision_state(t.car.collision_state,t.car.collision_support_since,
            t.car.published_support,t.car.collision_body_flag,double(static_cast<std::int32_t>(tick))*double(.00025F));
    }
    pair_frame_=contacts;
    return true;
}

bool RaceSession::prepare_style(const std::filesystem::path& root,std::string& error,unsigned event) {
    if(started_){error="style must be prepared before start";return false;}
    if(!style_.load(root,error,spec_.drift_mode))return false;
    if(spec_.drift_mode) {
        float area=0,grip=0;
        if(!ai_setup_performance(spec_.handling,area,grip)||!drift_reference_speed(spec_.handling,drift_reference_speed_)) {
            error="drift performance setup refused";return false;
        }
        drift_class_=area>500?0U:area>390?1U:area>280?2U:3U;
        if(!drift_competition_.load(root,event,spec_.drift_difficulty,drift_class_,area,0x60000000U,error,player_name_))return false;
    }
    style_shortcuts_.assign(course_.track.zones.size(),false);
    return true;
}
void RaceSession::update_style() {
    if(!style_.ready())return;
    if(timing_.stage==RaceStage::countdown) {
        style_.update(timing_.now_ticks,float(frame_seconds_));
        return;
    }
    if(timing_.stage!=RaceStage::racing)return;
    const auto& c=car();const auto& q=c.orientation;
    RaceStyleDrivingSample s;
    s.now=timing_.now_ticks;s.seconds=float(frame_seconds_);
    s.racing=true;s.finished=timing_.outcome==RaceOutcome::finished;
    s.race_seconds=float(timing_.now_ticks-timing_.race_start_ticks)*.00025F;
    s.racers=unsigned(rivals_.size()+1);s.position=player_position();
    s.new_checkpoint=timing_.racers[0].new_checkpoint;s.new_lap=timing_.racers[0].new_lap;
    s.speed=c.owner_speeds.magnitude;s.forward_speed=c.owner_speeds.forward;
    s.support=c.published_support;s.up_z=float(1-2*(q.x*q.x+q.y*q.y));
    const auto heading=track_angle(float(1-2*(q.y*q.y+q.z*q.z)),float(2*(q.x*q.y+q.w*q.z)));
    const auto velocity=track_angle(float(c.velocity.x),float(c.velocity.y));
    s.slip_degrees=float(static_cast<std::int16_t>(std::uint16_t(heading-velocity)))*(360.F/65536);
    s.heading_degrees=heading*(360.F/65536);s.yaw_rate=float(c.angular_velocity.z);
    // The existing solver publishes impulses in original t*m/s units. Contact
    // admission is the port hull/SAT adapter, not the original contact list.
    const auto strong=[](double x,double y,double z){return x*x+y*y+z*z>100;};
    for(const auto& t:track_telemetry_) {
        if(t.reset!=VehicleTrackReset::none)s.impact=true;
        for(std::size_t i=0;i<t.audio_contact_count;++i) {
            const auto& j=t.audio_contacts[i].impulse;
            // Do not spoil clean laps on ordinary upright road support.
            if((j.x*j.x+j.y*j.y>j.z*j.z || s.up_z<=.8F || !s.support) && strong(j.x,j.y,j.z))s.impact=true;
        }
    }
    for(std::size_t i=0;i<pair_frame_.count;++i) {
        const auto& p=pair_frame_.pairs[i];const auto& j=p.contact.impulse;
        if(p.first==0 && p.contact.applied && strong(j[0],j[1],j[2]))s.impact=true;
    }
    if(!s.support) {
        const auto ground=TrackVehicleGround(course_.world).cast(c.position,{0,0,-1},100);
        s.ground_valid=ground.hit;s.height=float(ground.distance-spec_.handling.centre_of_mass[2]);
    }
    const float point[2]={float(c.position.x),float(c.position.y)};
    for(std::size_t i=0;i<course_.track.zones.size();++i) {
        const auto& z=course_.track.zones[i];
        if(z.type!=9 || (z.direction_filter!=-1 && z.direction_filter!=int(course_.route.reverse)) ||
           point[0]<z.box_min[0] || point[0]>z.box_max[0] || point[1]<z.box_min[1] || point[1]>z.box_max[1])continue;
        if(!point_in_track_zone_polygon(point,z.vertices.data(),std::size_t(z.vertex_count)))continue;
        s.shortcut=!style_shortcuts_[i];style_shortcuts_[i]=true;break;
    }
    if(spec_.drift_mode) {
        DriftDrivingSample d;d.now=s.now;d.seconds=s.seconds;d.finished=s.finished;
        d.motion.speed=s.speed;d.motion.yaw=s.yaw_rate;d.motion.grade=drift_class_;
        // 464EA0 consumes the retained owner heading/velocity publication.
        d.motion.slip=s.forward_speed<1.F?0.F:c.owner_slip_angle;
        float percent=progress().previous_percent-course_.route.boundaries.at(std::size_t(course_.route.finish)).percent;
        if(course_.route.point_to_point){if(percent< -50)percent+=100;else if(percent>50)percent-=100;}
        if(percent<0)percent+=100;
        d.motion.progress=float(progress().lap)+percent*.01F;
        d.motion.blocked=progress().wrong_way||s.forward_speed<0;
        d.reference_speed=drift_reference_speed_;
        for(const auto& w:track_telemetry_.back().model.wheels)if(w.surface==3)++d.bonus_wheels;
        // 465830 publishes impulse magnitude; 43FBB0 admits walls (normal
        // z below .1), unsupported/tilted bodies and car pairs above 5.
        // This adapter uses port polygon contacts, not native pair lifetimes.
        for(const auto& t:track_telemetry_)for(std::size_t i=0;i<t.audio_contact_count;++i) {
            const auto& hit=t.audio_contacts[i];const auto& j=hit.impulse;
            const float strength=float(std::sqrt(j.x*j.x+j.y*j.y+j.z*j.z));
            if(strength>=5 && (std::abs(hit.normal.z)<.10000000149011612F || !s.support || s.up_z<=.9F)) {
                d.impact_strength=strength;d.wall_impact=true;
            }
        }
        for(std::size_t i=0;i<pair_frame_.count;++i) {
            const auto& pair=pair_frame_.pairs[i];const auto& j=pair.contact.impulse;
            const float strength=float(std::sqrt(j[0]*j[0]+j[1]*j[1]+j[2]*j[2]));
            if(pair.first==0 && pair.contact.applied && strength>=5) {
                d.impact_strength=strength;d.wall_impact=pair.other_kind!=0;
            }
        }
        drift_.update(style_,d);
        if(drift_.reset_requested())vehicle_.request_reset();
        drift_competition_.update(course_.track,course_.route.reverse,point,progress().max_lap,style_.item(0).total);
        return;
    }
    s.drafting=drafting_;
    if(s.impact)near_miss_.impact(s.now);
    // Query real convex hull separation, not center distance or an arbitrary
    // lane rectangle. Existing physics pair records suppress a collided pass.
    std::array<RaceNearPass,3+kRaceTrafficCapacity> passes{};std::size_t pass_count=0;
    const auto add_pass=[&](unsigned id,const VehicleState& other,const VehicleSpec& spec,const RaceContactBox& box,std::uint64_t epoch) {
        const double dx=c.position.x-other.position.x,dy=c.position.y-other.position.y,dz=c.position.z-other.position.z;
        if(dx*dx+dy*dy+dz*dz>400)return;
        const auto identity=(epoch<<8)|id;
        const double vx=c.velocity.x-other.velocity.x,vy=c.velocity.y-other.velocity.y,vz=c.velocity.z-other.velocity.z;
        const float relative=float(std::sqrt(vx*vx+vy*vy+vz*vz));
        if(s.speed<=20.116228103637695F||(!near_miss_.tracks(identity)&&relative<=40.23245620727539F))return;
        RaceHullQuery query;if(!query_race_hulls(c,spec_,contact_box_,other,spec,box,query,&near_pass_cache_[id-1]))return;
        bool contact=false;for(const auto& pair:world_contact_pairs_)if(pair.cars()&&pair.record.active&&
            ((pair.a==0&&pair.b==id)||(pair.b==0&&pair.a==id))){contact=true;break;}
        passes[pass_count++]={identity,float(query.distance),relative,contact};
    };
    for(unsigned i=0;i<rivals_.size();++i)if(!actor_ghost(i+1))add_pass(i+1,rivals_[i]->car(),rivals_[i]->spec_,rivals_[i]->contact_box_,0);
    if(traffic_)for(unsigned i=0;i<traffic_->contact_actors().size();++i){const auto& t=traffic_->contact_actors()[i];
        if(t.active)add_pass(4+i,t.car,t.setup.spec,t.box,contact_traffic_epochs_[i]);}
    near_miss_.update(style_,s.now,s.speed,passes.data(),pass_count);
    style_driving_.update(style_,s);
}

const std::vector<std::uint8_t>& RaceSession::record() const {
    if(record_dirty_) {encode();record_dirty_=false;}
    return record_;
}

void RaceSession::encode() const {
    ++work_.record_encodes;
    VehicleTelemetry telemetry[kDriveStepsPerFrame];
    for(std::size_t k=0;k<kDriveStepsPerFrame;++k) telemetry[k]=track_telemetry_[k].model;
    encode_drive_frame(timing_.step, inputs_, telemetry, view_, controls_.frame_state(), record_);
    for (const auto& t : track_telemetry_) {
        std::array<std::uint8_t, kVehicleTrackTelemetryRecordBytes> bytes{};
        encode_vehicle_track_telemetry(t, bytes);
        record_.insert(record_.end(), bytes.begin(), bytes.end());
    }
    const auto& p = progress();
    for (auto v : {p.lap,p.max_lap,p.checkpoint,p.max_checkpoint,p.next_boundary,
                   int(p.finished),int(p.wrong_way),p.zone,int(p.off_path),p.coordinate.node})
        u32(record_, static_cast<std::uint32_t>(v));
    for (float v : {p.coordinate.parameter,p.coordinate.fraction,p.coordinate.distance,
                    p.coordinate.tangent[0],p.coordinate.tangent[1],p.previous_percent}) f32(record_,v);
    const auto& t = timing_;
    for (auto v : {int(t.stage),int(t.outcome),t.now_ticks,t.step_ticks,
                   int(t.countdown_valid),t.countdown_start_ticks,int(t.go_valid),t.go_ticks,
                   int(t.race_start_valid),t.race_start_ticks,t.race_ticks,t.fastest_lap_ticks,
                   t.countdown_shown}) u32(record_,static_cast<std::uint32_t>(v));
    const auto& r = t.racers[0];
    for (auto v : {int(r.status),r.lap,r.highest_lap,r.checkpoint,r.highest_checkpoint,
                   int(r.new_lap),int(r.new_checkpoint),r.position,int(r.finish_valid),
                   r.finish_ticks,int(r.lap_shown_valid),r.lap_shown_since,r.lap_shown_ticks,
                   int(r.wrong_way),r.wrong_way_since}) u32(record_,static_cast<std::uint32_t>(v));
    for (std::size_t k = 0; k < kRaceBoundaryLimit; ++k) {
        for (auto v : {int(r.crossing_valid[k]),r.crossing_ticks[k],r.split_ticks[k],
                       r.previous_split_ticks[k],r.best_split_ticks[k],r.previous_best_split_ticks[k]})
            u32(record_,static_cast<std::uint32_t>(v));
    }
    for (int k = 0; k < kRaceLapSlots; ++k) {
        u32(record_,static_cast<std::uint32_t>(r.lap_ticks[k]));
        u32(record_,static_cast<std::uint32_t>(r.lap_total_ticks[k]));
    }
    if (steering_mode_ != RaceSteering::baseline) {
        u32(record_,0x31525453); // STR1, recovered-only state extension
        u32(record_,static_cast<std::uint32_t>(steering_mode_));
        const auto history = [this](const auto& h) {
            for (float v : h.values) f32(record_,v);
            for (float v : h.times) f32(record_,v);
            f32(record_,h.sum); f32(record_,h.average);
            for (auto v : {h.count,h.write,h.oldest}) u32(record_,static_cast<std::uint32_t>(v));
        };
        history(steering_state_.input); history(steering_state_.rate);
        u32(record_,steering_state_.angle); f32(record_,steering_state_.previous_limit);
        f32(record_,steering_state_.previous_raw); u32(record_,steering_state_.assist);
        for (auto v : {steering_steps_,shift_step_}) {
            u32(record_,static_cast<std::uint32_t>(v)); u32(record_,static_cast<std::uint32_t>(v>>32));
        }
        u32(record_,shifted_); f32(record_,steering_max_speed_);
    }
    if (spec_.nitrous.installed) {
        u32(record_,0x31534F4E); // NOS1 extension; uninstalled recordings unchanged.
        for (auto v:{nitrous_resource_.capacity,nitrous_resource_.step,
                     nitrous_resource_.remaining,nitrous_resource_.started,nitrous_clock_}) u32(record_,v);
        u32(record_,nitrous_engine_.active);f32(record_,nitrous_engine_.activation_seconds);
        f32(record_,turbo_.value);u32(record_,static_cast<std::uint32_t>(turbo_.mode));
    }
}

bool parse_race_input(const std::string& text, std::vector<RaceInputRun>& runs, std::string& error) {
    runs.clear();
    if (text.size() > 4*1024*1024) { error = "race input exceeds 4 MiB"; return false; }
    for (unsigned char c : text) if (c != '\n' && c != '\r' && c != '\t' && (c < 32 || c > 126)) {
        error = "race input must be ASCII"; return false;
    }
    std::istringstream stream(text); stream.imbue(std::locale::classic());
    std::string line; bool header = false; std::size_t total = 0;
    while (std::getline(stream,line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        std::istringstream row(line); row.imbue(std::locale::classic());
        row >> std::ws; if (row.eof()) continue;
        if (!header) {
            std::string magic, extra; int version = 0;
            if (!(row >> magic >> version) || magic != "NFSU_RACE_INPUT" || version != 1 || row >> extra) {
                error = "expected NFSU_RACE_INPUT 1"; return false;
            }
            header = true; continue;
        }
        long long ticks, buttons, steer; std::string extra;
        if (!(row >> ticks >> buttons >> steer) || row >> extra || ticks < 1 ||
            ticks > static_cast<long long>(kRaceInputMaxTicks-total) || buttons < 0 ||
            buttons > kGamepadButtonMask || steer < kAxisMin || steer > kAxisMax) {
            error = "invalid race input run or more than 120000 ticks"; runs.clear(); return false;
        }
        total += static_cast<std::size_t>(ticks);
        runs.push_back({static_cast<std::uint32_t>(ticks),static_cast<std::uint32_t>(buttons),
                        static_cast<std::int32_t>(steer)});
    }
    if (!header || runs.empty()) { error = "race input has no runs"; return false; }
    return true;
}

std::string format_race_input(const std::vector<RaceInputRun>& runs) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << "NFSU_RACE_INPUT 1\n";
    for (const auto& r : runs) out << r.ticks << ' ' << r.buttons << ' ' << r.steer << '\n';
    return out.str();
}
GamepadFrame race_input_frame(const RaceInputRun& run, std::uint64_t tick) {
    GamepadFrame pad; pad.time_us = tick*1000000/60; pad.pads[0].connected = true;
    pad.pads[0].buttons = run.buttons; pad.pads[0].axes[0] = run.steer; return pad;
}
void append_race_input(std::vector<RaceInputRun>& runs, const GamepadFrame& pad) {
    const auto& p = pad.pads[0];
    if (!runs.empty() && runs.back().buttons == p.buttons && runs.back().steer == p.axes[0])
        ++runs.back().ticks;
    else runs.push_back({1,p.buttons,p.axes[0]});
}

namespace {

// Authored and derived lane tables for RaceRouteDriver. Each is a list of
// {spline parameter, left offset in metres} control points, interpolated
// linearly and wrapped at the track's spline control-point count.
//
// kEvent1ForwardLanes and kEvent8ForwardLanes are the hand-authored tables
// of the pinned event-1 replay and the Atlantic drive, unchanged. Every
// other table was derived offline from the retail collision world of its
// own TRACKB bundle: at every 1/8-segment step along the race spline the
// drivable road corridor (first surface of kind road, slope cosine above
// 0.55) was swept laterally at the tracked road height, the corridor
// centers were straightened with moving-average passes clamped back into
// each sample's corridor, and the result was simplified to control points.
// The build/glm-circuits scratch of the authoring session holds the
// derivator; the per-circuit retail drive tests pin that each table drives
// its own event to completion without a reset.
constexpr double kEvent1ForwardLanes[][2] = {
    {0,0},{0.7,0},{1.15,-12},{1.5,-10},{2,-5},{2.5,-10},{4,-10},{5,0},
    {7.5,0},{8.2,6},{8.5,6},{8.8,0},{9.5,0},{12.1,0},{12.3,-10},
    {12.5,-15},{12.9,-10},{13.5,0},{21.5,0},{22,12},{23.5,12},
    {24.5,4},{25.2,4},{25.8,12},{28.9,12},{29.3,0},{30,0}};
constexpr double kEvent8ForwardLanes[][2] = {
    {0,0},{3,0},{3.2,4},{3.4,14},{3.6,26},{3.8,38},{4,47},{4.2,46},
    {4.4,35},{4.6,22},{4.8,10},{5,0},{5.4,0},{5.8,8},{6,12},{6.2,15},
    {6.4,18},{6.6,17},{6.8,15},{7,12},{7.2,10},{7.4,8},{7.6,6},{8,3},{8.2,0},
    {20.8,0},{21,7},{21.2,13},{21.4,13},{21.6,10},{21.8,3},{22,-5},
    {23.2,-5},{23.4,0},{23.6,15},{23.8,38},{24,58},{24.2,64},{24.4,74},
    {24.6,74},{24.8,59},{25,44},{25.2,29},{25.6,26},{26,15},{26.3,-6},
    {26.6,-6},{26.8,0},{27,8},{27.4,8},{27.6,0},{28.4,0},{28.8,-10},
    {29,-6},{29.2,5},{29.4,10},{30.5,10},{30.8,4},{31,8},{31.5,10},{31.8,6},{32,3},{32.3,0},{33,0}};

struct LaneTable {
    double period;
    const double (*lanes)[2];
    std::size_t count;
};

// The interpolated left offset of `table` at spline parameter `p`, wrapped
// at the table period; the same arithmetic the authored lines always used.
double lane_offset_at(const LaneTable& table, double p) {
    while (p >= table.period) p -= table.period;
    double offset = 0;
    for (std::size_t i = 1; i < table.count; ++i) {
        if (p > table.lanes[i][0]) continue;
        const double f = (p-table.lanes[i-1][0])/(table.lanes[i][0]-table.lanes[i-1][0]);
        offset = table.lanes[i-1][1] + f*(table.lanes[i][1]-table.lanes[i-1][1]);
        break;
    }
    return offset;
}

// Shifts a look-ahead target sideways by the offset, positive left of the
// forward tangent; the same tangent arithmetic the authored lines used.
void apply_lane_offset(const TrackPathSpline& spline, float parameter,
                       double offset, float target[3]) {
    float tangent[3]; evaluate_track_path(spline,parameter,1,tangent);
    const double length = vehicle_sqrt(double(tangent[0])*tangent[0]+double(tangent[1])*tangent[1]);
    target[0] -= static_cast<float>(offset*tangent[1]/std::max(length,1.0));
    target[1] += static_cast<float>(offset*tangent[0]/std::max(length,1.0));
}

} // namespace

constexpr double kCircuit1001ForwardLanes[][2] = {
    {0.00,0.0}, {14.38,-0.1}, {14.75,-0.3}, {15.00,-0.6},
    {15.12,-0.7}, {15.25,-0.9}, {15.38,-1.2}, {15.50,-1.5},
    {15.62,-1.8}, {15.75,-2.1}, {15.88,-2.6}, {16.00,-3.1},
    {16.12,-3.6}, {16.25,-4.2}, {16.38,-4.8}, {16.50,-5.5},
    {16.62,-6.3}, {16.75,-7.2}, {16.88,-8.0}, {17.00,-8.7},
    {17.12,-9.4}, {17.25,-10.7}, {17.38,-12.2}, {17.50,-13.2},
    {17.75,-12.7}, {17.88,-12.2}, {18.00,-12.0}, {18.25,-11.8},
    {18.38,-11.5}, {18.50,-11.1}, {18.62,-10.8}, {18.75,-10.4},
    {18.88,-10.0}, {19.00,-9.5}, {19.12,-9.0}, {19.25,-8.4},
    {19.38,-7.8}, {19.50,-7.1}, {19.62,-6.4}, {19.75,-5.7},
    {19.88,-5.1}, {20.00,-4.5}, {20.12,-3.3}, {20.25,-2.3},
    {20.38,-1.3}, {20.62,-1.8}, {20.75,-1.6}, {21.62,-1.5},
    {22.50,-1.3}, {29.25,-1.2}, {29.75,-1.1}, {30.12,-0.9},
    {30.38,-0.7}, {30.62,-0.5}, {30.75,-0.4}, {31.00,-0.2},
    {31.25,0.0}, {31.38,0.2}, {31.50,1.2}, {31.62,0.3},
    {48.00,0.4}, {48.38,0.6}, {48.62,0.8}, {48.75,0.9},
    {48.88,1.1}, {49.00,1.3}, {49.12,1.6}, {49.25,1.9},
    {49.38,2.3}, {49.50,2.7}, {49.62,3.2}, {49.75,3.8},
    {49.88,4.4}, {50.00,5.2}, {50.12,6.0}, {50.25,6.7},
    {50.38,7.5}, {50.50,8.5}, {50.62,9.7}, {50.75,10.7},
    {50.88,11.1}, {51.00,12.6}, {51.12,13.2}, {51.25,16.2},
    {51.38,18.7}, {51.50,21.7}, {51.62,24.2}, {51.75,-27.2},
    {51.88,-28.2}, {52.00,-28.7}, {52.12,-29.2}, {52.38,-29.7},
    {52.88,-29.3}, {53.12,-29.0}, {53.25,-28.8}, {53.38,-28.5},
    {53.50,-28.2}, {53.62,-27.7}, {53.75,-27.3}, {53.88,-26.7},
    {54.00,-26.0}, {54.12,-25.2}, {54.25,-24.3}, {54.38,-23.2},
    {54.50,-22.1}, {54.62,-20.8}, {54.75,-19.5}, {54.88,-17.8},
    {55.00,-16.0}, {55.12,-14.3}, {55.25,-12.9}, {55.38,-11.5},
    {55.50,-10.1}, {55.62,-3.8}, {55.75,-2.3}, {55.88,-3.8},
    {56.38,-3.6}, {56.75,-3.5}, {56.88,-3.3}, {57.00,-3.1},
    {57.12,-2.9}, {57.38,-2.8}, {57.62,-2.6}, {58.00,-2.4},
    {58.88,-2.3}, {67.75,-2.2}, {68.12,-2.0}, {68.38,-1.8},
    {68.62,-1.6}, {68.75,-1.4}, {68.88,-1.3}, {69.00,-1.1},
    {69.12,-0.9}, {69.25,-0.6}, {69.38,-0.3}, {69.50,0.0},
    {69.62,0.3}, {69.75,0.5}, {69.88,0.7}, {70.00,1.0},
    {70.12,2.2}, {70.25,2.7}, {70.38,1.9}, {70.50,2.1},
    {70.88,2.2}, {71.38,2.4}, {71.75,2.5}, {72.38,2.6},
    {97.62,2.5}, {98.00,2.4}, {98.25,2.1}, {98.62,2.0},
    {99.00,0.0},
};

constexpr double kCircuit1001ReverseLanes[][2] = {
    {0.00,0.0}, {14.38,-0.1}, {14.75,-0.3}, {15.00,-0.6},
    {15.12,-0.7}, {15.25,-0.9}, {15.38,-1.2}, {15.50,-1.5},
    {15.62,-1.8}, {15.75,-2.1}, {15.88,-2.6}, {16.00,-3.1},
    {16.12,-3.6}, {16.25,-4.2}, {16.38,-4.8}, {16.50,-5.5},
    {16.62,-6.3}, {16.75,-7.2}, {16.88,-8.0}, {17.00,-8.7},
    {17.12,-9.4}, {17.25,-10.7}, {17.38,-12.2}, {17.50,-13.2},
    {17.75,-12.7}, {17.88,-12.2}, {18.00,-12.0}, {18.25,-11.8},
    {18.38,-11.5}, {18.50,-11.1}, {18.62,-10.8}, {18.75,-10.4},
    {18.88,-10.0}, {19.00,-9.5}, {19.12,-9.0}, {19.25,-8.4},
    {19.38,-7.8}, {19.50,-7.1}, {19.62,-6.4}, {19.75,-5.7},
    {19.88,-5.1}, {20.00,-4.5}, {20.12,-3.3}, {20.25,-2.3},
    {20.38,-1.3}, {20.62,-1.8}, {20.75,-1.6}, {21.62,-1.5},
    {22.50,-1.3}, {29.25,-1.2}, {29.75,-1.1}, {30.12,-0.9},
    {30.38,-0.7}, {30.62,-0.5}, {30.75,-0.4}, {31.00,-0.2},
    {31.25,0.0}, {31.38,0.2}, {31.50,1.2}, {31.62,0.3},
    {48.00,0.4}, {48.38,0.6}, {48.62,0.8}, {48.75,0.9},
    {48.88,1.1}, {49.00,1.3}, {49.12,1.6}, {49.25,1.9},
    {49.38,2.3}, {49.50,2.7}, {49.62,3.2}, {49.75,3.8},
    {49.88,4.4}, {50.00,5.2}, {50.12,6.0}, {50.25,6.7},
    {50.38,7.5}, {50.50,8.5}, {50.62,9.7}, {50.75,10.7},
    {50.88,11.1}, {51.00,12.6}, {51.12,13.2}, {51.25,16.2},
    {51.38,18.7}, {51.50,21.7}, {51.62,24.2}, {51.75,-27.2},
    {51.88,-28.2}, {52.00,-28.7}, {52.12,-29.2}, {52.38,-29.7},
    {52.88,-29.3}, {53.12,-29.0}, {53.25,-28.8}, {53.38,-28.5},
    {53.50,-28.2}, {53.62,-27.7}, {53.75,-27.3}, {53.88,-26.7},
    {54.00,-26.0}, {54.12,-25.2}, {54.25,-24.3}, {54.38,-23.2},
    {54.50,-22.1}, {54.62,-20.8}, {54.75,-19.5}, {54.88,-17.8},
    {55.00,-16.0}, {55.12,-14.3}, {55.25,-12.9}, {55.38,-11.5},
    {55.50,-10.1}, {55.62,-3.8}, {55.75,-2.3}, {55.88,-3.8},
    {56.38,-3.6}, {56.75,-3.5}, {56.88,-3.3}, {57.00,-3.1},
    {57.12,-2.9}, {57.38,-2.8}, {57.62,-2.6}, {58.00,-2.4},
    {58.88,-2.3}, {67.75,-2.2}, {68.12,-2.0}, {68.38,-1.8},
    {68.62,-1.6}, {68.75,-1.4}, {68.88,-1.3}, {69.00,-1.1},
    {69.12,-0.9}, {69.25,-0.6}, {69.38,-0.3}, {69.50,0.0},
    {69.62,0.3}, {69.75,0.5}, {69.88,0.7}, {70.00,1.0},
    {70.12,2.2}, {70.25,2.7}, {70.38,1.9}, {70.50,2.1},
    {70.88,2.2}, {71.38,2.4}, {71.75,2.5}, {72.38,2.6},
    {97.62,2.5}, {98.00,2.4}, {98.25,2.1}, {98.62,2.0},
    {99.00,0.0},
};

constexpr double kCircuit1002ReverseLanes[][2] = {
    {0.00,-0.0}, {1.75,-0.1}, {2.25,-0.3}, {2.88,-0.5},
    {3.12,-0.6}, {3.25,-1.2}, {3.38,-1.7}, {3.50,-2.2},
    {3.62,1.7}, {3.88,1.3}, {4.00,1.7}, {5.00,1.8},
    {5.25,2.0}, {5.50,2.2}, {5.75,2.4}, {6.00,2.6},
    {6.25,2.8}, {6.50,3.7}, {6.62,3.0}, {7.50,3.2},
    {7.75,2.7}, {8.00,2.3}, {8.12,2.0}, {8.50,1.8},
    {8.62,3.7}, {8.75,-1.2}, {8.88,-3.2}, {9.00,4.2},
    {9.12,1.2}, {9.25,0.8}, {9.62,0.7}, {9.88,0.6},
    {10.00,0.4}, {10.25,0.2}, {10.50,0.0}, {10.62,-0.7},
    {10.88,-0.4}, {11.75,-0.5}, {13.12,-0.6}, {13.75,-0.8},
    {13.88,1.2}, {14.00,-1.7}, {14.12,-1.5}, {14.25,-1.7},
    {14.50,-1.9}, {14.62,-2.1}, {14.75,-2.5}, {14.88,-2.7},
    {15.00,-2.9}, {15.12,-3.1}, {15.25,-3.3}, {15.38,-3.5},
    {15.50,-5.2}, {15.62,-3.9}, {15.88,-4.1}, {16.50,-4.3},
    {17.88,-4.1}, {18.25,-3.9}, {18.50,-3.8}, {18.75,-3.5},
    {19.00,-3.4}, {19.25,-3.2}, {19.50,-2.3}, {19.62,-2.8},
    {19.88,-2.7}, {20.50,-2.5}, {21.12,-2.4}, {27.75,-2.3},
    {28.62,-2.1}, {29.00,-2.0}, {29.25,-1.8}, {29.75,-1.6},
    {30.00,-0.0},
};

constexpr double kCircuit1004ForwardLanes[][2] = {
    {0.00,0.6}, {0.75,0.7}, {1.00,0.8}, {1.25,1.0},
    {1.50,1.7}, {1.62,1.2}, {2.12,1.4}, {2.75,1.5},
    {3.62,1.7}, {8.50,1.5}, {8.75,1.4}, {9.00,1.1},
    {9.12,1.0}, {9.25,0.8}, {9.38,0.6}, {9.50,0.3},
    {9.62,0.0}, {9.75,-0.3}, {9.88,-0.6}, {10.00,-1.0},
    {10.12,-1.5}, {10.25,-2.0}, {10.38,-2.5}, {10.50,-2.9},
    {10.62,-3.5}, {10.75,-3.9}, {10.88,-5.7}, {11.38,-6.7},
    {11.50,-5.5}, {11.62,-6.2}, {11.75,-5.5}, {12.00,-5.3},
    {12.12,-5.2}, {12.25,-4.9}, {12.38,-4.7}, {12.75,-4.2},
    {12.88,-3.0}, {13.00,-2.6}, {13.12,-2.2}, {13.25,-1.8},
    {13.38,-1.5}, {13.50,1.2}, {13.62,0.7}, {13.75,0.1},
    {13.88,0.3}, {14.25,0.5}, {14.62,0.6}, {14.88,0.8},
    {15.25,1.0}, {15.62,1.1}, {16.88,1.2}, {18.00,1.4},
    {18.25,1.5}, {18.50,1.8}, {18.62,2.0}, {18.75,2.2},
    {18.88,2.5}, {19.00,2.9}, {19.12,3.3}, {19.25,3.8},
    {19.38,4.3}, {19.50,5.0}, {19.62,5.8}, {19.75,6.6},
    {19.88,7.5}, {20.00,8.5}, {20.12,9.5}, {20.25,10.9},
    {20.38,12.5}, {20.50,13.6}, {20.62,14.7}, {20.75,15.8},
    {20.88,16.8}, {21.00,21.7}, {21.12,24.2}, {21.25,20.4},
    {21.38,20.8}, {22.12,20.7}, {22.38,20.5}, {22.50,20.3},
    {22.62,20.1}, {22.75,19.8}, {22.88,19.5}, {23.00,19.1},
    {23.12,18.7}, {23.25,18.3}, {23.38,17.8}, {23.50,17.3},
    {23.62,16.8}, {23.75,16.2}, {23.88,15.7}, {24.00,15.2},
    {24.12,14.8}, {24.25,14.4}, {24.38,12.3}, {24.75,12.5},
    {25.75,12.3}, {26.12,12.2}, {26.50,12.0}, {26.88,11.8},
    {27.50,12.3}, {27.62,13.5}, {27.75,14.0}, {27.88,15.0},
    {28.00,15.3}, {28.12,15.5}, {28.50,18.2}, {28.62,24.2},
    {28.75,16.2}, {28.88,13.7}, {29.00,12.9}, {29.12,11.8},
    {29.25,10.6}, {29.38,9.1}, {29.50,7.3}, {29.62,6.1},
    {29.75,4.9}, {29.88,1.3}, {30.00,-0.2}, {30.12,-1.2},
    {30.25,-0.3}, {30.38,-1.2}, {30.62,-2.0}, {30.75,-2.4},
    {30.88,-2.6}, {31.00,-2.7}, {31.12,-4.2}, {31.25,-6.2},
    {31.38,-5.2}, {31.50,-3.2}, {31.62,-2.9}, {31.75,-2.7},
    {31.88,-2.4}, {32.00,-1.9}, {32.12,-1.2}, {32.25,-0.4},
    {32.38,0.3}, {32.50,0.9}, {32.62,1.5}, {32.75,2.0},
    {32.88,2.6}, {33.00,5.2}, {33.12,6.2}, {33.25,4.7},
    {34.25,4.3}, {34.62,4.2}, {35.00,0.6},
};

constexpr double kCircuit1004ReverseLanes[][2] = {
    {0.00,0.6}, {0.75,0.7}, {1.00,0.8}, {1.25,1.0},
    {1.50,1.7}, {1.62,1.2}, {2.12,1.4}, {2.75,1.5},
    {3.62,1.7}, {8.50,1.5}, {8.75,1.4}, {9.00,1.1},
    {9.12,1.0}, {9.25,0.8}, {9.38,0.6}, {9.50,0.3},
    {9.62,0.0}, {9.75,-0.3}, {9.88,-0.6}, {10.00,-1.0},
    {10.12,-1.5}, {10.25,-2.0}, {10.38,-2.5}, {10.50,-2.9},
    {10.62,-3.5}, {10.75,-3.9}, {10.88,-5.7}, {11.38,-6.7},
    {11.50,-5.5}, {11.62,-6.2}, {11.75,-5.5}, {12.00,-5.3},
    {12.12,-5.2}, {12.25,-4.9}, {12.38,-4.7}, {12.75,-4.2},
    {12.88,-3.0}, {13.00,-2.6}, {13.12,-2.2}, {13.25,-1.8},
    {13.38,-1.5}, {13.50,1.2}, {13.62,0.7}, {13.75,0.1},
    {13.88,0.3}, {14.25,0.5}, {14.62,0.6}, {14.88,0.8},
    {15.25,1.0}, {15.62,1.1}, {16.88,1.2}, {18.00,1.4},
    {18.25,1.5}, {18.50,1.8}, {18.62,2.0}, {18.75,2.2},
    {18.88,2.5}, {19.00,2.9}, {19.12,3.3}, {19.25,3.8},
    {19.38,4.3}, {19.50,5.0}, {19.62,5.8}, {19.75,6.6},
    {19.88,7.5}, {20.00,8.5}, {20.12,9.5}, {20.25,10.9},
    {20.38,12.5}, {20.50,13.6}, {20.62,14.7}, {20.75,15.8},
    {20.88,16.8}, {21.00,21.7}, {21.12,24.2}, {21.25,20.4},
    {21.38,20.8}, {22.12,20.7}, {22.38,20.5}, {22.50,20.3},
    {22.62,20.1}, {22.75,19.8}, {22.88,19.5}, {23.00,19.1},
    {23.12,18.7}, {23.25,18.3}, {23.38,17.8}, {23.50,17.3},
    {23.62,16.8}, {23.75,16.2}, {23.88,15.7}, {24.00,15.2},
    {24.12,14.8}, {24.25,14.4}, {24.38,12.3}, {24.75,12.5},
    {25.75,12.3}, {26.12,12.2}, {26.50,12.0}, {26.88,11.8},
    {27.50,12.3}, {27.62,13.5}, {27.75,14.0}, {27.88,15.0},
    {28.00,15.3}, {28.12,15.5}, {28.50,18.2}, {28.62,24.2},
    {28.75,16.2}, {28.88,13.7}, {29.00,12.9}, {29.12,11.8},
    {29.25,10.6}, {29.38,9.1}, {29.50,7.3}, {29.62,6.1},
    {29.75,4.9}, {29.88,1.3}, {30.00,-0.2}, {30.12,-1.2},
    {30.25,-0.3}, {30.38,-1.2}, {30.62,-2.0}, {30.75,-2.4},
    {30.88,-2.6}, {31.00,-2.7}, {31.12,-4.2}, {31.25,-6.2},
    {31.38,-5.2}, {31.50,-3.2}, {31.62,-2.9}, {31.75,-2.7},
    {31.88,-2.4}, {32.00,-1.9}, {32.12,-1.2}, {32.25,-0.4},
    {32.38,0.3}, {32.50,0.9}, {32.62,1.5}, {32.75,2.0},
    {32.88,2.6}, {33.00,5.2}, {33.12,6.2}, {33.25,4.7},
    {34.25,4.3}, {34.62,4.2}, {35.00,0.6},
};

constexpr double kCircuit1005ReverseLanes[][2] = {
    {0.00,-1.0}, {0.50,-1.1}, {0.75,-1.2}, {1.00,-1.4},
    {1.25,-1.6}, {1.50,-2.2}, {1.88,-1.8}, {2.50,-1.5},
    {2.75,-1.3}, {3.12,-1.2}, {3.25,0.2}, {3.38,-1.2},
    {3.50,-1.0}, {4.38,-0.9}, {4.62,-0.7}, {4.75,-0.6},
    {4.88,-0.4}, {5.00,-0.3}, {5.12,-0.1}, {5.25,0.2},
    {5.38,0.5}, {5.50,0.9}, {5.62,1.2}, {5.75,1.6},
    {6.00,2.6}, {6.12,3.3}, {6.38,3.9}, {6.50,4.4},
    {6.62,0.8}, {6.75,10.7}, {6.88,9.2}, {7.00,7.7},
    {7.12,7.3}, {7.25,7.0}, {7.50,6.8}, {7.88,6.6},
    {8.12,6.3}, {8.25,5.8}, {8.38,6.3}, {8.62,6.2},
    {9.25,6.0}, {9.75,5.9}, {10.75,5.8}, {11.62,5.7},
    {12.00,5.5}, {12.25,5.4}, {12.50,5.2}, {12.75,5.0},
    {12.88,4.8}, {13.00,4.7}, {13.12,4.5}, {13.25,4.3},
    {13.38,4.0}, {13.50,3.8}, {13.62,3.5}, {13.75,3.2},
    {13.88,2.9}, {14.00,2.6}, {14.12,2.2}, {14.25,1.8},
    {14.38,1.4}, {14.50,1.0}, {14.62,0.7}, {14.75,0.3},
    {14.88,-0.7}, {15.00,-1.2}, {15.25,-0.8}, {16.50,-0.3},
    {16.62,-0.2}, {16.75,-0.1}, {16.88,0.1}, {17.00,0.3},
    {17.12,0.6}, {17.25,0.7}, {17.38,0.9}, {17.50,1.0},
    {17.62,1.2}, {17.75,2.2}, {17.88,2.7}, {18.00,1.5},
    {18.62,1.2}, {18.75,0.9}, {18.88,0.8}, {19.00,0.6},
    {19.12,0.3}, {19.25,0.1}, {19.38,-0.1}, {19.50,-0.3},
    {19.62,-0.4}, {19.75,-0.7}, {19.88,-3.2}, {20.00,-0.7},
    {20.62,-0.4}, {20.75,-0.1}, {21.00,0.2}, {21.25,0.4},
    {21.38,2.7}, {21.50,0.4}, {21.88,0.2}, {22.00,-0.0},
    {22.12,-0.3}, {22.25,-0.9}, {22.38,-1.4}, {22.50,-2.0},
    {22.62,-2.7}, {22.75,-3.5}, {22.88,-4.3}, {23.00,-5.1},
    {23.12,-6.5}, {23.25,-7.9}, {23.38,-9.2}, {23.62,-10.2},
    {23.75,-9.3}, {23.88,-18.7}, {24.00,-19.2}, {24.12,-18.2},
    {24.25,-15.2}, {24.38,-11.2}, {24.50,-8.4}, {24.62,-6.8},
    {24.75,-4.5}, {24.88,-2.3}, {25.00,-0.4}, {25.12,1.2},
    {25.25,4.2}, {25.38,12.2}, {25.50,11.2}, {25.62,9.2},
    {25.75,6.7}, {25.88,5.8}, {26.00,5.7}, {26.25,4.9},
    {26.38,4.2}, {26.50,-0.2}, {26.62,2.8}, {26.75,2.4},
    {26.88,2.0}, {27.12,1.7}, {27.25,1.5}, {27.50,1.2},
    {27.62,1.0}, {27.75,0.8}, {27.88,0.7}, {28.00,0.5},
    {28.25,0.3}, {28.50,0.1}, {28.75,-0.0}, {29.25,-0.1},
    {32.38,-0.3}, {33.00,-1.0},
};

constexpr double kCircuit1006ForwardLanes[][2] = {
    {0.00,-0.0}, {1.75,-0.2}, {2.12,-0.3}, {2.38,-0.4},
    {2.62,-0.6}, {2.88,-0.8}, {3.25,-1.0}, {3.62,-2.2},
    {3.75,-1.2}, {3.88,-1.4}, {4.00,-1.6}, {4.38,-1.8},
    {4.62,9.2}, {4.75,-9.2}, {4.88,-6.2}, {5.00,-3.2},
    {5.12,-2.5}, {5.25,-1.9}, {5.62,-1.7}, {5.75,-1.3},
    {5.88,0.2}, {6.00,-0.8}, {6.25,-0.6}, {6.50,-0.4},
    {6.75,-0.3}, {7.00,-0.1}, {7.25,0.2}, {7.50,0.4},
    {7.75,0.6}, {8.00,0.8}, {8.25,0.9}, {8.50,1.7},
    {8.62,1.0}, {9.25,0.8}, {9.38,0.5}, {9.62,0.2},
    {10.00,-0.2}, {10.25,0.2}, {10.38,-1.3}, {10.50,1.7},
    {10.62,-3.8}, {10.75,-7.4}, {10.88,-8.9}, {11.00,-10.3},
    {11.12,-11.7}, {11.25,-12.9}, {11.38,-18.2}, {11.50,-20.2},
    {11.62,-20.7}, {11.75,-18.7}, {11.88,-19.1}, {12.12,-19.0},
    {12.50,-19.1}, {13.00,-18.8}, {13.12,-19.1}, {14.25,-18.9},
    {14.75,-18.8}, {15.00,-18.7}, {15.25,-18.5}, {15.38,-18.3},
    {15.50,-18.2}, {15.62,-18.0}, {15.75,-17.7}, {15.88,-17.4},
    {16.00,-17.1}, {16.12,-16.7}, {16.25,-16.2}, {16.38,-15.6},
    {16.50,-15.0}, {16.62,-14.2}, {16.75,-13.4}, {16.88,-12.5},
    {17.00,-11.5}, {17.12,-10.4}, {17.25,-9.2}, {17.38,-7.9},
    {17.50,-6.5}, {17.62,-5.2}, {17.75,-3.9}, {17.88,-2.7},
    {18.00,-0.3}, {18.12,1.7}, {18.25,2.7}, {18.38,3.2},
    {18.50,3.7}, {18.62,3.2}, {18.88,3.4}, {19.25,3.5},
    {19.62,3.6}, {20.00,3.8}, {20.38,4.0}, {20.75,3.9},
    {21.00,4.0}, {21.12,4.7}, {21.25,5.8}, {21.38,4.2},
    {21.50,12.7}, {21.62,21.7}, {21.75,-24.7}, {21.88,-30.7},
    {22.00,34.7}, {22.12,32.7}, {22.25,28.2}, {22.38,22.7},
    {22.50,16.7}, {22.62,12.5}, {22.75,10.7}, {22.88,9.1},
    {23.00,5.1}, {23.12,1.2}, {23.25,-2.6}, {23.38,-5.7},
    {23.50,-8.1}, {23.62,-10.8}, {23.75,-19.2}, {23.88,-22.2},
    {24.00,-27.2}, {24.12,-22.7}, {24.25,-22.3}, {24.62,-21.8},
    {25.00,-21.6}, {25.25,-21.3}, {25.38,-20.9}, {25.50,-20.4},
    {25.62,-19.9}, {25.75,-19.3}, {25.88,-18.7}, {26.00,-18.0},
    {26.12,-17.2}, {26.25,-16.4}, {26.38,-15.5}, {26.50,-14.5},
    {26.62,-13.5}, {26.75,-12.6}, {26.88,-11.8}, {27.00,-11.0},
    {27.12,-8.8}, {27.25,-7.8}, {27.38,-7.3}, {27.50,-6.8},
    {27.62,-7.3}, {27.88,-6.8}, {28.00,-7.2}, {28.50,-7.0},
    {29.12,-6.9}, {32.00,-6.8}, {32.88,-6.6}, {33.25,-6.5},
    {33.50,-6.4}, {33.75,-6.2}, {34.00,-6.0}, {34.25,-5.8},
    {34.50,-5.6}, {34.62,-5.3}, {35.00,-5.1}, {35.12,-5.0},
    {35.25,-4.8}, {35.38,-4.3}, {35.50,-4.4}, {35.62,-4.3},
    {35.88,-4.2}, {36.00,-4.0}, {36.12,-3.9}, {36.25,-3.6},
    {36.50,-3.4}, {36.75,-3.2}, {37.00,-0.0},
};

constexpr double kCircuit1006ReverseLanes[][2] = {
    {0.00,-0.0}, {1.75,-0.2}, {2.12,-0.3}, {2.38,-0.4},
    {2.62,-0.6}, {2.88,-0.8}, {3.25,-1.0}, {3.62,-2.2},
    {3.75,-1.2}, {3.88,-1.4}, {4.00,-1.6}, {4.38,-1.8},
    {4.62,9.2}, {4.75,-9.2}, {4.88,-6.2}, {5.00,-3.2},
    {5.12,-2.5}, {5.25,-1.9}, {5.62,-1.7}, {5.75,-1.3},
    {5.88,0.2}, {6.00,-0.8}, {6.25,-0.6}, {6.50,-0.4},
    {6.75,-0.3}, {7.00,-0.1}, {7.25,0.2}, {7.50,0.4},
    {7.75,0.6}, {8.00,0.8}, {8.25,0.9}, {8.50,1.7},
    {8.62,1.0}, {9.25,0.8}, {9.38,0.5}, {9.62,0.2},
    {10.00,-0.2}, {10.25,0.2}, {10.38,-1.3}, {10.50,1.7},
    {10.62,-3.8}, {10.75,-7.4}, {10.88,-8.9}, {11.00,-10.3},
    {11.12,-11.7}, {11.25,-12.9}, {11.38,-18.2}, {11.50,-20.2},
    {11.62,-20.7}, {11.75,-18.7}, {11.88,-19.1}, {12.12,-19.0},
    {12.50,-19.1}, {13.00,-18.8}, {13.12,-19.1}, {14.25,-18.9},
    {14.75,-18.8}, {15.00,-18.7}, {15.25,-18.5}, {15.38,-18.3},
    {15.50,-18.2}, {15.62,-18.0}, {15.75,-17.7}, {15.88,-17.4},
    {16.00,-17.1}, {16.12,-16.7}, {16.25,-16.2}, {16.38,-15.6},
    {16.50,-15.0}, {16.62,-14.2}, {16.75,-13.4}, {16.88,-12.5},
    {17.00,-11.5}, {17.12,-10.4}, {17.25,-9.2}, {17.38,-7.9},
    {17.50,-6.5}, {17.62,-5.2}, {17.75,-3.9}, {17.88,-2.7},
    {18.00,-0.3}, {18.12,1.7}, {18.25,2.7}, {18.38,3.2},
    {18.50,3.7}, {18.62,3.2}, {18.88,3.4}, {19.25,3.5},
    {19.62,3.6}, {20.00,3.8}, {20.38,4.0}, {20.75,3.9},
    {21.00,4.0}, {21.12,4.7}, {21.25,5.8}, {21.38,4.2},
    {21.50,12.7}, {21.62,21.7}, {21.75,-24.7}, {21.88,-30.7},
    {22.00,34.7}, {22.12,32.7}, {22.25,28.2}, {22.38,22.7},
    {22.50,16.7}, {22.62,12.5}, {22.75,10.7}, {22.88,9.1},
    {23.00,5.1}, {23.12,1.2}, {23.25,-2.6}, {23.38,-5.7},
    {23.50,-8.1}, {23.62,-10.8}, {23.75,-19.2}, {23.88,-22.2},
    {24.00,-27.2}, {24.12,-22.7}, {24.25,-22.3}, {24.62,-21.8},
    {25.00,-21.6}, {25.25,-21.3}, {25.38,-20.9}, {25.50,-20.4},
    {25.62,-19.9}, {25.75,-19.3}, {25.88,-18.7}, {26.00,-18.0},
    {26.12,-17.2}, {26.25,-16.4}, {26.38,-15.5}, {26.50,-14.5},
    {26.62,-13.5}, {26.75,-12.6}, {26.88,-11.8}, {27.00,-11.0},
    {27.12,-8.8}, {27.25,-7.8}, {27.38,-7.3}, {27.50,-6.8},
    {27.62,-7.3}, {27.88,-6.8}, {28.00,-7.2}, {28.50,-7.0},
    {29.12,-6.9}, {32.00,-6.8}, {32.88,-6.6}, {33.25,-6.5},
    {33.50,-6.4}, {33.75,-6.2}, {34.00,-6.0}, {34.25,-5.8},
    {34.50,-5.6}, {34.62,-5.3}, {35.00,-5.1}, {35.12,-5.0},
    {35.25,-4.8}, {35.38,-4.3}, {35.50,-4.4}, {35.62,-4.3},
    {35.88,-4.2}, {36.00,-4.0}, {36.12,-3.9}, {36.25,-3.6},
    {36.50,-3.4}, {36.75,-3.2}, {37.00,-0.0},
};

constexpr double kCircuit1007ForwardLanes[][2] = {
    {0.00,0.0}, {4.25,0.1}, {10.62,-0.1}, {10.88,-0.3},
    {11.00,-0.5}, {11.12,-0.7}, {11.25,-1.0}, {11.38,-1.3},
    {11.50,-1.7}, {11.62,-2.2}, {11.75,-2.7}, {11.88,-3.3},
    {12.00,-4.1}, {12.12,-4.9}, {12.25,-5.7}, {12.38,-6.6},
    {12.50,-7.6}, {12.62,-9.1}, {12.75,-10.6}, {12.88,-11.9},
    {13.00,-12.5}, {13.12,-12.9}, {13.25,-14.7}, {13.38,-20.7},
    {13.50,-22.7}, {13.62,-19.7}, {13.75,-12.2}, {13.88,-10.5},
    {14.00,-9.7}, {14.12,-4.8}, {14.25,-2.8}, {14.50,-1.8},
    {14.88,-2.8}, {15.00,-5.6}, {15.12,-6.5}, {15.25,-7.2},
    {15.38,-8.1}, {15.50,-9.5}, {15.62,-11.4}, {15.75,-12.3},
    {15.88,-12.9}, {16.00,-12.3}, {16.12,-13.8}, {16.25,-19.8},
    {16.38,-21.8}, {16.50,-26.9}, {16.62,-31.7}, {16.75,-41.2},
    {16.88,-48.0}, {17.25,54.7}, {17.50,-48.0}, {17.75,-38.7},
    {17.88,-44.2}, {18.00,-29.6}, {18.12,-26.9}, {18.25,-24.3},
    {18.38,-24.0}, {18.62,-23.7}, {18.88,-18.2}, {19.00,-13.8},
    {19.12,-11.8}, {19.25,-9.9}, {19.38,2.7}, {19.50,7.7},
    {19.62,0.2}, {19.75,-2.4}, {19.88,-2.7}, {20.00,-3.0},
    {20.38,-13.7}, {20.50,-2.5}, {20.75,-2.1}, {20.88,-1.8},
    {21.00,-1.4}, {21.12,0.7}, {21.25,3.7}, {21.38,0.8},
    {21.50,1.2}, {21.62,1.4}, {21.88,1.6}, {23.00,1.4},
    {23.25,1.3}, {23.38,0.3}, {23.50,1.0}, {23.75,0.8},
    {24.12,0.7}, {24.62,0.5}, {25.12,0.4}, {26.00,0.3},
    {27.00,0.0},
};

constexpr double kCircuit1007ReverseLanes[][2] = {
    {0.00,0.0}, {4.25,0.1}, {10.62,-0.1}, {10.88,-0.3},
    {11.00,-0.5}, {11.12,-0.7}, {11.25,-1.0}, {11.38,-1.3},
    {11.50,-1.7}, {11.62,-2.2}, {11.75,-2.7}, {11.88,-3.3},
    {12.00,-4.1}, {12.12,-4.9}, {12.25,-5.7}, {12.38,-6.6},
    {12.50,-7.6}, {12.62,-9.1}, {12.75,-10.6}, {12.88,-11.9},
    {13.00,-12.5}, {13.12,-12.9}, {13.25,-14.7}, {13.38,-20.7},
    {13.50,-22.7}, {13.62,-19.7}, {13.75,-12.2}, {13.88,-10.5},
    {14.00,-9.7}, {14.12,-4.8}, {14.25,-2.8}, {14.50,-1.8},
    {14.88,-2.8}, {15.00,-5.6}, {15.12,-6.5}, {15.25,-7.2},
    {15.38,-8.1}, {15.50,-9.5}, {15.62,-11.4}, {15.75,-12.3},
    {15.88,-12.9}, {16.00,-12.3}, {16.12,-13.8}, {16.25,-19.8},
    {16.38,-21.8}, {16.50,-26.9}, {16.62,-31.7}, {16.75,-41.2},
    {16.88,-48.0}, {17.25,54.7}, {17.50,-48.0}, {17.75,-38.7},
    {17.88,-44.2}, {18.00,-29.6}, {18.12,-26.9}, {18.25,-24.3},
    {18.38,-24.0}, {18.62,-23.7}, {18.88,-18.2}, {19.00,-13.8},
    {19.12,-11.8}, {19.25,-9.9}, {19.38,2.7}, {19.50,7.7},
    {19.62,0.2}, {19.75,-2.4}, {19.88,-2.7}, {20.00,-3.0},
    {20.38,-13.7}, {20.50,-2.5}, {20.75,-2.1}, {20.88,-1.8},
    {21.00,-1.4}, {21.12,0.7}, {21.25,3.7}, {21.38,0.8},
    {21.50,1.2}, {21.62,1.4}, {21.88,1.6}, {23.00,1.4},
    {23.25,1.3}, {23.38,0.3}, {23.50,1.0}, {23.75,0.8},
    {24.12,0.7}, {24.62,0.5}, {25.12,0.4}, {26.00,0.3},
    {27.00,0.0},
};

constexpr double kCircuit1008ForwardLanes[][2] = {
    {0.00,-7.9}, {0.12,-8.1}, {0.50,-10.2}, {0.62,-9.7},
    {0.75,-6.9}, {0.88,-6.6}, {1.00,-6.3}, {1.12,-6.0},
    {1.25,-5.3}, {1.38,-3.3}, {1.50,-3.8}, {1.62,-4.1},
    {1.75,-4.0}, {2.12,-3.8}, {2.62,-3.7}, {3.50,-3.9},
    {3.62,-4.0}, {3.75,-4.2}, {3.88,-4.4}, {4.00,-4.7},
    {4.12,-5.1}, {4.25,-5.4}, {4.38,-5.9}, {4.50,-6.4},
    {4.62,-7.1}, {4.75,-8.1}, {4.88,-8.7}, {5.00,-8.8},
    {5.12,-9.7}, {5.25,-11.2}, {5.50,-11.8}, {5.62,-13.8},
    {5.75,-14.7}, {5.88,-17.0}, {6.00,-24.7}, {6.12,-39.2},
    {6.25,-48.0}, {6.38,60.2}, {6.88,55.2}, {7.00,57.7},
    {7.12,59.2}, {7.25,58.7}, {7.38,57.7}, {7.50,57.2},
    {7.62,56.2}, {7.75,55.7}, {8.00,55.2}, {8.12,54.7},
    {8.25,52.7}, {8.38,51.2}, {8.50,49.2}, {8.62,46.7},
    {8.75,42.7}, {8.88,-13.2}, {9.00,-10.2}, {9.12,-6.7},
    {9.25,-3.2}, {9.38,2.0}, {9.50,3.6}, {9.62,4.5},
    {9.75,5.0}, {10.00,6.7}, {10.12,9.7}, {10.25,11.2},
    {10.38,9.7}, {10.50,7.7}, {10.88,7.5}, {11.00,7.3},
    {11.12,6.9}, {11.25,6.3}, {11.38,5.8}, {11.62,5.5},
    {11.75,5.1}, {11.88,4.6}, {12.00,4.1}, {12.12,3.5},
    {12.25,3.0}, {12.38,2.2}, {12.50,1.4}, {12.62,0.5},
    {12.75,-0.4}, {12.88,-1.4}, {13.00,-2.4}, {13.12,-4.0},
    {13.25,-5.6}, {13.38,-6.6}, {13.50,-7.6}, {13.62,-8.6},
    {13.75,-9.5}, {13.88,-17.2}, {14.12,-12.5}, {14.25,-12.8},
    {14.38,-12.5}, {14.88,-12.1}, {15.25,-11.9}, {15.38,-11.8},
    {15.50,-10.8}, {15.62,-11.4}, {15.75,-11.3}, {15.88,-11.1},
    {16.12,-11.0}, {16.25,-10.8}, {16.38,-10.7}, {16.50,-10.4},
    {16.62,-10.2}, {16.75,-10.0}, {16.88,-9.7}, {17.00,-9.3},
    {17.12,-9.0}, {17.25,-8.5}, {17.50,-8.0}, {17.62,-7.1},
    {17.88,-6.4}, {18.00,-5.8}, {18.12,-9.7}, {18.25,-4.1},
    {18.38,1.2}, {18.50,-0.3}, {18.62,-1.6}, {18.75,-0.8},
    {18.88,-1.2}, {19.00,-0.8}, {19.12,2.2}, {19.25,-1.2},
    {19.38,-1.0}, {19.88,-1.2}, {20.00,-1.6}, {20.25,-2.2},
    {20.50,-2.5}, {20.62,-2.6}, {20.88,-2.8}, {21.00,-7.9},
};

constexpr double kCircuit1008ReverseLanes[][2] = {
    {0.00,-7.9}, {0.12,-8.1}, {0.50,-10.2}, {0.62,-9.7},
    {0.75,-6.9}, {0.88,-6.6}, {1.00,-6.3}, {1.12,-6.0},
    {1.25,-5.3}, {1.38,-3.3}, {1.50,-3.8}, {1.62,-4.1},
    {1.75,-4.0}, {2.12,-3.8}, {2.62,-3.7}, {3.50,-3.9},
    {3.62,-4.0}, {3.75,-4.2}, {3.88,-4.4}, {4.00,-4.7},
    {4.12,-5.1}, {4.25,-5.4}, {4.38,-5.9}, {4.50,-6.4},
    {4.62,-7.1}, {4.75,-8.1}, {4.88,-8.7}, {5.00,-8.8},
    {5.12,-9.7}, {5.25,-11.2}, {5.50,-11.8}, {5.62,-13.8},
    {5.75,-14.7}, {5.88,-17.0}, {6.00,-24.7}, {6.12,-39.2},
    {6.25,-48.0}, {6.38,60.2}, {6.88,55.2}, {7.00,57.7},
    {7.12,59.2}, {7.25,58.7}, {7.38,57.7}, {7.50,57.2},
    {7.62,56.2}, {7.75,55.7}, {8.00,55.2}, {8.12,54.7},
    {8.25,52.7}, {8.38,51.2}, {8.50,49.2}, {8.62,46.7},
    {8.75,42.7}, {8.88,-13.2}, {9.00,-10.2}, {9.12,-6.7},
    {9.25,-3.2}, {9.38,2.0}, {9.50,3.6}, {9.62,4.5},
    {9.75,5.0}, {10.00,6.7}, {10.12,9.7}, {10.25,11.2},
    {10.38,9.7}, {10.50,7.7}, {10.88,7.5}, {11.00,7.3},
    {11.12,6.9}, {11.25,6.3}, {11.38,5.8}, {11.62,5.5},
    {11.75,5.1}, {11.88,4.6}, {12.00,4.1}, {12.12,3.5},
    {12.25,3.0}, {12.38,2.2}, {12.50,1.4}, {12.62,0.5},
    {12.75,-0.4}, {12.88,-1.4}, {13.00,-2.4}, {13.12,-4.0},
    {13.25,-5.6}, {13.38,-6.6}, {13.50,-7.6}, {13.62,-8.6},
    {13.75,-9.5}, {13.88,-17.2}, {14.12,-12.5}, {14.25,-12.8},
    {14.38,-12.5}, {14.88,-12.1}, {15.25,-11.9}, {15.38,-11.8},
    {15.50,-10.8}, {15.62,-11.4}, {15.75,-11.3}, {15.88,-11.1},
    {16.12,-11.0}, {16.25,-10.8}, {16.38,-10.7}, {16.50,-10.4},
    {16.62,-10.2}, {16.75,-10.0}, {16.88,-9.7}, {17.00,-9.3},
    {17.12,-9.0}, {17.25,-8.5}, {17.50,-8.0}, {17.62,-7.1},
    {17.88,-6.4}, {18.00,-5.8}, {18.12,-9.7}, {18.25,-4.1},
    {18.38,1.2}, {18.50,-0.3}, {18.62,-1.6}, {18.75,-0.8},
    {18.88,-1.2}, {19.00,-0.8}, {19.12,2.2}, {19.25,-1.2},
    {19.38,-1.0}, {19.88,-1.2}, {20.00,-1.6}, {20.25,-2.2},
    {20.50,-2.5}, {20.62,-2.6}, {20.88,-2.8}, {21.00,-7.9},
};
// Drift track 1301, forward, derived the same way from TRACKB1301's own
// collision world: 88 samples, none missing, and a corridor that stays
// centred - the simplified table collapses to the zero offset. The seven
// further drift tracks (1302-1308, owner task 2026-10-14) derive the same
// way: none missing, every corridor centred, every table the zero offset.
constexpr double kDrift1301ForwardLanes[][2] = {
    {0.00,0.0},{11.00,0.0},
};
constexpr double kDrift1302ForwardLanes[][2] = {
    {0.00,0.0},{22.00,0.0},
};
constexpr double kDrift1303ForwardLanes[][2] = {
    {0.00,0.0},{37.00,0.0},
};
constexpr double kDrift1304ForwardLanes[][2] = {
    {0.00,0.0},{40.00,0.0},
};
constexpr double kDrift1305ForwardLanes[][2] = {
    {0.00,0.0},{36.00,0.0},
};
constexpr double kDrift1306ForwardLanes[][2] = {
    {0.00,0.0},{53.00,0.0},
};
constexpr double kDrift1307ForwardLanes[][2] = {
    {0.00,0.0},{37.00,0.0},
};
constexpr double kDrift1308ForwardLanes[][2] = {
    {0.00,0.0},{41.00,0.0},
};
// The forward drag lanes, derived from each TRACKB's own collision world
// the same way (corridor sweep at tracked road height; the drag splines
// run beside or across the highway rather than down it, so the offsets
// carry the lane the corridor centers on). The reverse derivation is not
// wired: its output came out byte-identical to the forward table, which
// a backwards traversal of the same corridor cannot produce.
constexpr double kDrag1201ForwardLanes[][2] = {
    {0.00,18.7},{0.12,15.0},{0.38,14.8},{0.50,14.6},{0.62,14.5},
    {0.75,14.4},{0.88,13.9},{1.00,12.8},{1.75,12.7},{2.38,12.3},{2.50,12.5},
    {3.62,12.3},{5.12,12.2},{5.62,12.0},{5.88,11.9},{6.12,11.7},{6.38,11.5},
    {6.50,11.3},{6.62,11.2},{6.75,11.0},{6.88,10.8},{7.00,10.6},{7.12,10.4},
    {7.25,10.2},{7.38,9.9},{7.62,9.5},{7.75,9.1},{8.00,8.7},{8.12,8.3},
    {8.38,6.8},{8.50,5.8},{8.75,7.8},{8.88,16.6},{9.00,25.7},{10.00,18.7},
};
constexpr double kDrag1202ForwardLanes[][2] = {
    {0.00,0.0},{12.00,0.0},
};
constexpr double kDrag1206ForwardLanes[][2] = {
    {0.00,10.7},{0.12,9.3},{0.38,8.8},{0.50,9.2},{0.75,9.1},{1.50,9.0},
    {2.50,8.8},{3.88,8.7},{4.25,8.5},{4.50,8.4},{4.75,8.2},{4.88,8.0},
    {5.00,7.8},{5.12,7.7},{5.25,7.5},{5.50,7.1},{5.62,6.7},{5.88,6.3},
    {6.00,6.0},{6.12,8.7},{6.25,3.3},{6.50,3.8},{7.00,3.7},{7.62,3.5},
    {9.38,3.7},{9.62,4.0},{9.75,4.1},{9.88,4.2},{10.00,4.4},{10.12,4.6},
    {10.25,5.1},{10.50,5.3},{10.62,5.5},{10.75,5.6},{11.00,10.7},
};
constexpr double kDrag1207ForwardLanes[][2] = {
    {0.00,0.0},{3.12,0.1},{3.62,0.3},{4.12,0.4},{4.38,0.7},{4.50,0.5},
    {5.62,0.6},{13.62,0.5},{14.00,0.0},
};
constexpr double kDrag1210ForwardLanes[][2] = {
    {0.00,18.7},{0.12,15.0},{0.38,14.8},{0.50,14.6},{0.62,14.5},
    {0.75,14.4},{0.88,13.9},{1.00,12.8},{1.75,12.7},{2.38,12.3},{2.50,12.5},
    {3.62,12.3},{5.12,12.2},{5.62,12.0},{5.88,11.9},{6.12,11.7},{6.38,11.5},
    {6.50,11.3},{6.62,11.2},{6.75,11.0},{6.88,10.8},{7.00,10.6},{7.12,10.4},
    {7.25,10.2},{7.38,9.9},{7.62,9.5},{7.75,9.1},{8.00,8.7},{8.12,8.3},
    {8.38,6.8},{8.50,5.8},{8.75,7.8},{8.88,16.6},{9.00,25.7},{10.00,18.7},
};
constexpr double kDrag1214ForwardLanes[][2] = {
    {0.00,10.7},{0.12,9.3},{0.38,8.8},{0.50,9.2},{0.75,9.1},{1.50,9.0},
    {2.50,8.8},{3.88,8.7},{4.25,8.5},{4.50,8.4},{4.75,8.2},{4.88,8.0},
    {5.00,7.8},{5.12,7.7},{5.25,7.5},{5.50,7.1},{5.62,6.7},{5.88,6.3},
    {6.00,6.0},{6.12,8.7},{6.25,3.3},{6.50,3.8},{7.00,3.7},{7.62,3.5},
    {9.38,3.7},{9.62,4.0},{9.75,4.1},{9.88,4.2},{10.00,4.4},{10.12,4.6},
    {10.25,5.1},{10.50,5.3},{10.62,5.5},{10.75,5.6},{11.00,10.7},
};

// Track 1003, reverse, derived the same way for the lap-knockout
// events 5 and 10 (240 samples, none missing).
constexpr double kCircuit1003ReverseLanes[][2] = {
    {0.00,-1.9},{0.25,-1.8},{0.38,-1.9},{0.50,-1.8},{0.62,-0.8},
{0.75,-2.8},{0.88,-3.1},{1.00,-3.4},{1.12,-3.6},{1.25,-3.8},{1.38,-7.2},
{1.50,-4.7},{1.75,-4.9},{2.00,-4.8},{2.12,-5.6},
{2.38,-5.7},{2.50,-5.9},{2.62,-6.0},{2.75,-6.1},{2.88,-7.2},
{3.00,-6.5},{3.25,-6.7},{3.75,-6.8},{7.62,-6.6},{8.12,-6.5},
{8.50,-6.3},{8.88,-6.2},
{9.00,-5.8},{9.12,-6.1},{10.62,-6.2},{10.88,-6.4},{11.00,-6.5},{11.12,-6.7},
{11.25,-6.8},{11.50,-7.1},{11.62,-7.5},{11.88,-7.7},
{12.00,-7.9},{12.12,-6.8},{12.25,-8.4},{12.38,-11.2},{12.50,-9.7},{12.62,-8.7},
{13.25,-8.6},{13.38,-8.4},
{13.50,-8.3},{13.62,-7.8},{13.75,-8.1},{14.00,-7.9},{14.38,-7.7},{14.62,-7.5},{14.75,-7.4},{14.88,-7.3},
{15.00,-7.1},{15.12,-6.9},{15.25,-6.7},{15.38,-6.5},{15.50,-6.3},{15.62,-6.0},
{15.75,-5.7},{15.88,-5.4},{16.00,-5.1},{16.12,-4.9},{16.25,-4.6},{16.38,-4.3},
{16.50,-3.3},{17.25,-3.2},{17.50,-3.0},{17.75,-2.9},{18.00,-2.8},{18.12,-2.3},{18.25,-2.6},{18.75,-2.5},{19.62,-2.4},{25.25,-2.2},{25.75,-2.1},{26.00,-1.9},{26.25,-1.7},{26.38,-1.6},{26.50,-1.4},{26.62,-1.2},{26.75,-1.1},{27.00,-0.8},{27.12,-0.5},{27.25,0.1},{27.50,0.3},{27.75,-5.2},{27.88,5.7},{28.25,4.7},{28.38,3.9},
{28.50,3.4},{28.62,3.3},{29.00,2.9},{29.12,2.6},
{29.25,2.3},{29.50,0.3},{29.62,1.7},{29.88,1.5},
{30.00,-1.9},
};

const LaneTable* driving_line_table(RaceDrivingLine line) {
    static const LaneTable tables[] = {
        {30, kEvent1ForwardLanes, sizeof(kEvent1ForwardLanes)/sizeof(*kEvent1ForwardLanes)},
        {33, kEvent8ForwardLanes, sizeof(kEvent8ForwardLanes)/sizeof(*kEvent8ForwardLanes)},
        {99, kCircuit1001ForwardLanes, sizeof(kCircuit1001ForwardLanes)/sizeof(*kCircuit1001ForwardLanes)},
        {99, kCircuit1001ReverseLanes, sizeof(kCircuit1001ReverseLanes)/sizeof(*kCircuit1001ReverseLanes)},
        {30, kCircuit1002ReverseLanes, sizeof(kCircuit1002ReverseLanes)/sizeof(*kCircuit1002ReverseLanes)},
        {35, kCircuit1004ForwardLanes, sizeof(kCircuit1004ForwardLanes)/sizeof(*kCircuit1004ForwardLanes)},
        {35, kCircuit1004ReverseLanes, sizeof(kCircuit1004ReverseLanes)/sizeof(*kCircuit1004ReverseLanes)},
        {33, kCircuit1005ReverseLanes, sizeof(kCircuit1005ReverseLanes)/sizeof(*kCircuit1005ReverseLanes)},
        {37, kCircuit1006ForwardLanes, sizeof(kCircuit1006ForwardLanes)/sizeof(*kCircuit1006ForwardLanes)},
        {37, kCircuit1006ReverseLanes, sizeof(kCircuit1006ReverseLanes)/sizeof(*kCircuit1006ReverseLanes)},
        {27, kCircuit1007ForwardLanes, sizeof(kCircuit1007ForwardLanes)/sizeof(*kCircuit1007ForwardLanes)},
        {27, kCircuit1007ReverseLanes, sizeof(kCircuit1007ReverseLanes)/sizeof(*kCircuit1007ReverseLanes)},
        {21, kCircuit1008ForwardLanes, sizeof(kCircuit1008ForwardLanes)/sizeof(*kCircuit1008ForwardLanes)},
        {21, kCircuit1008ReverseLanes, sizeof(kCircuit1008ReverseLanes)/sizeof(*kCircuit1008ReverseLanes)},
        {11, kDrift1301ForwardLanes, sizeof(kDrift1301ForwardLanes)/sizeof(*kDrift1301ForwardLanes)},
        {22, kDrift1302ForwardLanes, sizeof(kDrift1302ForwardLanes)/sizeof(*kDrift1302ForwardLanes)},
        {37, kDrift1303ForwardLanes, sizeof(kDrift1303ForwardLanes)/sizeof(*kDrift1303ForwardLanes)},
        {40, kDrift1304ForwardLanes, sizeof(kDrift1304ForwardLanes)/sizeof(*kDrift1304ForwardLanes)},
        {36, kDrift1305ForwardLanes, sizeof(kDrift1305ForwardLanes)/sizeof(*kDrift1305ForwardLanes)},
        {53, kDrift1306ForwardLanes, sizeof(kDrift1306ForwardLanes)/sizeof(*kDrift1306ForwardLanes)},
        {37, kDrift1307ForwardLanes, sizeof(kDrift1307ForwardLanes)/sizeof(*kDrift1307ForwardLanes)},
        {41, kDrift1308ForwardLanes, sizeof(kDrift1308ForwardLanes)/sizeof(*kDrift1308ForwardLanes)},
        {10, kDrag1201ForwardLanes, sizeof(kDrag1201ForwardLanes)/sizeof(*kDrag1201ForwardLanes)},
        {12, kDrag1202ForwardLanes, sizeof(kDrag1202ForwardLanes)/sizeof(*kDrag1202ForwardLanes)},
        {11, kDrag1206ForwardLanes, sizeof(kDrag1206ForwardLanes)/sizeof(*kDrag1206ForwardLanes)},
        {14, kDrag1207ForwardLanes, sizeof(kDrag1207ForwardLanes)/sizeof(*kDrag1207ForwardLanes)},
        {10, kDrag1210ForwardLanes, sizeof(kDrag1210ForwardLanes)/sizeof(*kDrag1210ForwardLanes)},
        {11, kDrag1214ForwardLanes, sizeof(kDrag1214ForwardLanes)/sizeof(*kDrag1214ForwardLanes)},
        {30, kCircuit1003ReverseLanes, sizeof(kCircuit1003ReverseLanes)/sizeof(*kCircuit1003ReverseLanes)},
    };
    const std::size_t index = static_cast<std::size_t>(line) -
        static_cast<std::size_t>(RaceDrivingLine::event1_forward);
    return index < sizeof(tables)/sizeof(*tables) ? &tables[index] : nullptr;
}


GamepadFrame RaceRouteDriver::sample(const RaceSession& session) {
    if (race_car_held(session.timing())) return race_input_frame({1,0,0},session.timing().step);
    if (ticks_++ % 6 == 0) {
        const auto& state = session.car(); const auto& coord = session.progress().coordinate;
        const auto& spline = session.course().route.spline;
        float derivative[3], target[3]; evaluate_track_path(spline,coord.parameter,1,derivative);
        const double norm = vehicle_sqrt(double(derivative[0])*derivative[0]+double(derivative[1])*derivative[1]);
        // Fixed look-ahead does not collapse when braking or accelerating.
        const double v = speed(state);
        // Arena corners need a shorter pursuit chord than open road. This
        // controls the host regression pilot only, not player/retail AI physics.
        const bool arena=line_>=RaceDrivingLine::drift1301_forward&&line_<=RaceDrivingLine::drift1308_forward;
        const double look_ahead=arena?6.0:12.0;
        const float parameter = coord.parameter + static_cast<float>(look_ahead/std::max(norm,1.0));
        evaluate_track_path(spline,parameter,0,target);
        // Authored lane offsets follow the progress spline, which crosses
        // medians and even runs outside barriers: it measures progress, not
        // a drivable centre line. Positive is left; linear ramps between
        // control points, metres, not pose changes. Controller input only.
        if (const LaneTable* lanes = driving_line_table(line_))
            apply_lane_offset(spline, parameter, lane_offset_at(*lanes, parameter), target);
        float preview[3];
        // Host route-driver adapter only: the recorded fixed line predates
        // traffic. Steer around occupied lane space with normal pad input;
        // collision, pose, timing and production player controls stay intact.
        if(session.traffic()) {
            const double dx=target[0]-state.position.x,dy=target[1]-state.position.y;
            const double d=vehicle_sqrt(dx*dx+dy*dy),tx=dx/std::max(d,1.0),ty=dy/std::max(d,1.0);
            double closest=35,shift=0;
            for(const auto& actor:session.traffic()->actors())if(actor.active) {
                const auto& p=actor.car.position;
                const double x=p.x-state.position.x,y=p.y-state.position.y;
                const double ahead=x*tx+y*ty,lateral=-x*ty+y*tx;
                if(ahead< -4||ahead>=closest||std::abs(lateral)>4||std::abs(p.z-state.position.z)>3)continue;
                const double clearance=(actor.box.high.y-actor.box.low.y)*.5+2;
                double preferred=lateral>=0?lateral-clearance:lateral+clearance;
                const auto road=[&](double offset) {
                    const float point[]={float(target[0]-offset*ty),float(target[1]+offset*tx),float(state.position.z)};
                    TrackContact contact;
                    return query_track_contact_point(session.course().world,point,2,8,contact)==TrackContactStatus::hit;
                };
                if(!road(preferred)){preferred=lateral>=0?lateral+clearance:lateral-clearance;if(!road(preferred))continue;}
                shift=preferred;closest=ahead;
            }
            target[0]-=float(shift*ty);target[1]+=float(shift*tx);
        }
        evaluate_track_path(spline,coord.parameter+static_cast<float>(30/std::max(norm,1.0)),1,preview);
        const double pn = vehicle_sqrt(double(preview[0])*preview[0]+double(preview[1])*preview[1]);
        const double ux = derivative[0]/std::max(norm,1.0)-preview[0]/std::max(pn,1.0);
        const double uy = derivative[1]/std::max(norm,1.0)-preview[1]/std::max(pn,1.0);
        const double turn = vehicle_sqrt(ux*ux+uy*uy);
        desired_speed_ = std::max(6.0,10/(1+2*turn));
        const auto& q = state.orientation;
        const double fx = 1-2*(q.y*q.y+q.z*q.z), fy = 2*(q.x*q.y+q.w*q.z);
        const double dx = target[0]-state.position.x, dy = target[1]-state.position.y;
        const double distance = vehicle_sqrt(dx*dx+dy*dy);
        const double steer = std::clamp(-(fx*dy-fy*dx)/std::max(distance,1.0)*1.8,-1.0,1.0);
        int raw = steer == 0 ? 0 : static_cast<int>(steer*27000+(steer>0?3000:-3000));
        raw = (raw/256)*256;
        // Stuck recovery for the derived circuit lanes only: the pinned
        // event-1 replay and the Atlantic drive keep their exact authored
        // input. A lane that pins the car into a wall at standstill would
        // otherwise deadlock (no speed means no steering authority); holding
        // the brake engages the reverse assist, and backing with opposite
        // lock swings the nose toward the next target - a three point turn
        // when the car ends up facing away from it. The normal controller
        // resumes one and a half seconds later. Controller input only,
        // never pose, progress or reset state.
        const bool recovery_line = line_ != RaceDrivingLine::event1_forward &&
                                   line_ != RaceDrivingLine::event8_forward;
        if (recovery_line) {
            if (recovering_) {
                // Keep reversing until the nose well faces the next target
                // (a full three point turn when the car ended up backward),
                // then give the normal controller at least a moment. A
                // 90-degree exit is too early: the pursuit steer stays
                // saturated and the car donuts in place.
                ++recovering_;
                const double face = (fx*dx + fy*dy)/std::max(distance,1.0);
                if ((face > 0.5 && recovering_ > 20) || recovering_ > 90) {
                    recovering_ = 0; stuck_ = 0;
                } else {
                    // In reverse, lock opposite to the side the target sits
                    // on: the nose swings toward the target.
                    const double side = fx*dy - fy*dx;
                    const int lock = side >= 0.0 ? -26624 : 26624;
                    held_ = race_input_frame({1,gamepad_button_bit(GamepadButton::trigger_left),lock},
                                            session.timing().step);
                    held_.time_us = session.timing().step*1000000/60; return held_;
                }
            } else {
                stuck_ = v < 0.6 ? stuck_ + 1 : 0;
                if (stuck_ >= 12) { recovering_ = 1; }
            }
        }
        held_ = race_input_frame({1,0,raw},session.timing().step);
    }
    // Steering look-ahead remains coarse; pedal feedback is every frame.
    if(!recovering_) {
        const double v=speed(session.car());
        if(v<desired_speed_-0.5)accelerating_=true;
        if(v>desired_speed_+0.5)accelerating_=false;
        held_.pads[0].buttons=accelerating_?gamepad_button_bit(GamepadButton::trigger_right):0;
        if(v>desired_speed_+0.7)held_.pads[0].buttons=gamepad_button_bit(GamepadButton::trigger_left);
    }
    // Deliberate controller-only smoke/braking witness on the opening straight.
    if(line_==RaceDrivingLine::event1_forward && ticks_>=120 && ticks_<132)
        held_.pads[0].buttons=gamepad_button_bit(GamepadButton::trigger_right)|
                              gamepad_button_bit(GamepadButton::face_south);
    held_.time_us = session.timing().step*1000000/60;
    return held_;
}

RaceDrivingLine race_event_driving_line(std::uint32_t event) noexcept {
    switch (event) {
    case 1: case 121: return RaceDrivingLine::event1_forward;
    case 8: return RaceDrivingLine::event8_forward;
    case 19: case 113: case 120: return RaceDrivingLine::circuit1001_forward;
    case 96: return RaceDrivingLine::circuit1001_reverse;
    case 59: return RaceDrivingLine::circuit1002_reverse;
    case 71: case 81: case 89: return RaceDrivingLine::circuit1004_forward;
    case 87: case 104: return RaceDrivingLine::circuit1004_reverse;
    case 37: case 52: return RaceDrivingLine::circuit1005_reverse;
    case 122: return RaceDrivingLine::circuit1006_forward;
    case 16: case 35: case 53: return RaceDrivingLine::circuit1006_reverse;
    case 99: return RaceDrivingLine::circuit1007_forward;
    case 107: case 111: return RaceDrivingLine::circuit1007_reverse;
    case 32: return RaceDrivingLine::circuit1008_forward;
    case 45: case 75: return RaceDrivingLine::circuit1008_reverse;
    case 3: return RaceDrivingLine::drift1301_forward;
    case 13: case 18: return RaceDrivingLine::drift1302_forward;
    case 25: case 33: case 50: return RaceDrivingLine::drift1303_forward;
    case 28: case 40: case 64: case 73: return RaceDrivingLine::drift1304_forward;
    case 44: case 56: return RaceDrivingLine::drift1305_forward;
    case 65: case 86: return RaceDrivingLine::drift1306_forward;
    case 90: case 93: return RaceDrivingLine::drift1307_forward;
    case 105: case 109: return RaceDrivingLine::drift1308_forward;
    // The lap-knockout events ride the lanes their tracks already have.
    case 5: case 10: return RaceDrivingLine::circuit1003_reverse;
    case 24: case 39: return RaceDrivingLine::circuit1006_reverse;
    case 31: return RaceDrivingLine::event8_forward;
    case 49: return RaceDrivingLine::circuit1008_forward;
    case 58: return RaceDrivingLine::circuit1008_reverse;
    case 60: return RaceDrivingLine::circuit1006_forward;
    case 72: case 103: return RaceDrivingLine::circuit1001_forward;
    case 84: return RaceDrivingLine::circuit1001_reverse;
    case 91: return RaceDrivingLine::circuit1007_forward;
    case 21: case 46: return RaceDrivingLine::drag1201_forward;
    case 11: case 42: return RaceDrivingLine::drag1202_forward;
    case 36: case 62: case 79: return RaceDrivingLine::drag1206_forward;
    case 83: return RaceDrivingLine::drag1207_forward;
    case 51: case 88: return RaceDrivingLine::drag1210_forward;
    case 70: return RaceDrivingLine::drag1214_forward;
    default: return RaceDrivingLine::progress_spline;
    }
}

std::vector<std::string> race_session_hud(const RaceSession& session, std::uint32_t event_number) {
    RaceHud hud;
    if (!race_timing_hud(session.course().route,session.timing_config(),session.timing(),0,hud))
        return {"RACE HUD UNAVAILABLE"};
    const auto& r = session.timing().racers[0];
    const int lap = std::min(hud.lap+1,hud.laps);
    const int current = hud.finished ? r.lap_ticks[r.highest_lap] :
        session.timing().step_ticks-r.crossing_ticks[0];
    const std::string last = r.highest_lap > 0 && r.highest_lap < kRaceLapSlots
        ? format_race_ticks(r.lap_ticks[r.highest_lap]) : "--:--.--";
    return {"EVENT " + std::to_string(event_number) + "  LAP " + std::to_string(lap) + " / " + std::to_string(hud.laps),
        "CURRENT " + (hud.lap_time_valid ? format_race_ticks(current) : "--:--.--") + "  LAST " + last,
        hud.finished ? "FINISHED " + format_race_ticks(r.finish_ticks) :
        hud.countdown==4 ? "GO" : hud.countdown ? "COUNTDOWN " + std::to_string(hud.countdown) : "RACING",
        "W/S GAS/BRAKE  A/D STEER  SPACE HANDBRAKE  R RESET  C CAMERA  ESC QUIT"};
}

bool compose_race_frame(const Frame& track, const Frame& car, Frame& composed) {
    if (!track.width() || track.width()!=car.width() || track.height()!=car.height()) return false;
    composed = track;
    for (std::size_t i=0;i<track.depth().size();++i) {
        if (car.ids()[i]==kNoTriangle || car.depth()[i]>track.depth()[i]) continue;
        std::copy_n(car.rgba_.data()+4*i,4,composed.rgba_.data()+4*i);
        composed.depth_[i]=car.depth_[i];
        composed.ids_[i]=car.ids_[i];
        composed.blended_[i]=car.blended_[i];
    }
    return true;
}

bool compose_race_frame(const Frame& track, const Frame& car, std::vector<unsigned char>& rgba) {
    Frame composed;
    if (!compose_race_frame(track, car, composed)) return false;
    rgba = composed.rgba();
    return true;
}
} // namespace nfsu
