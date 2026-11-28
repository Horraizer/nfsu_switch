#include "nfsu/lan_race.h"
#include "nfsu/platform.h"
#include "nfsu/car_collision_hull.h"
#include "nfsu/car_assets.h"
#include "nfsu/race_setup.h"
#include "nfsu/race_events.h"
#include "nfsu/texture.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace nfsu {
bool load_lan_car_names(const std::filesystem::path& root,std::vector<std::string>& names,std::string& error){
    std::vector<unsigned char> bytes;std::vector<RaceTableIssue> issues;
    if(load_race_image(root/"Global/GLOBALB.BUN",bytes,issues))
        names=playable_car_names(parse_car_type_table(bytes.data(),bytes.size()));
    if(names.empty()){error="LAN playable car roster unavailable";return false;}
    error.clear();return true;
}
const std::string* find_lan_car_name(const std::vector<std::string>& names,std::uint32_t hash){
    const auto it=std::find_if(names.begin(),names.end(),[hash](const auto& name){return hash?bin_hash(name)==hash:name=="CIVIC";});
    return it==names.end()?nullptr:&*it;
}
VehicleUpgradeLevels lan_performance_levels(unsigned tier){
    const int level=static_cast<int>(tier);
    VehicleUpgradeLevels result;
    result.weight=result.suspension=result.engine_a=result.induction=result.nitrous=
        result.engine_b=result.gearbox=result.tyres=result.grip=level;
    return result;
}
bool LanRaceWorld::load(const std::filesystem::path& root,std::string& error,const LanRaceOptions& options){
    if(!valid_lan_options(options)){error="invalid LAN world settings";return false;}
    const auto& course=kLanCourses[options.course];
    return load_course(root,course.event,course.round,options,false,error);
}
bool LanRaceWorld::load_local(const std::filesystem::path& root,unsigned event,unsigned round,QuickRaceMode mode,
                             const LanRaceOptions& options,std::string& error,unsigned traffic_density,
                             const std::vector<RaceTrafficSetup>& traffic){
    return load_course(root,event,round,options,true,error,mode,traffic_density,traffic);
}
bool LanRaceWorld::load_course(const std::filesystem::path& root,unsigned event,unsigned round,
                              const LanRaceOptions& options,bool split,std::string& error,QuickRaceMode mode,unsigned traffic_density,
                              const std::vector<RaceTrafficSetup>& traffic){
    if(session_){error="two-player world already loaded";return false;}
    SupportedRaceSelection race;
    if(options.laps<1||options.laps>6||options.tier>3||traffic_density>3||
       (!traffic_density&&!traffic.empty())||
       !supported_race_selection(event,round,race)||(race.lap_skip&&options.laps!=1)) {
        error="invalid two-player course settings";return false;
    }
    if(!load_vehicle_track_course(root/("Tracks/TRACKB"+std::to_string(race.slot.track)+".lzc"),
        options.laps,race.slot.direction,race.lap_skip,0,course_,error))return false;
    const bool drag=mode==QuickRaceMode::drag;
    const bool drift=mode==QuickRaceMode::drift;
    if(split&&mode!=QuickRaceMode::circuit&&mode!=QuickRaceMode::sprint&&!drag&&!drift) {
        error="unsupported split-screen race mode";return false;
    }
    if(drag)course_.start.gearbox=VehicleGearbox::manual;
    if(drift&&traffic_density) {error="drift requires traffic off";return false;}
    std::vector<std::string> names;
    if(!load_lan_car_names(root,names,error))return false;
    const auto levels=lan_performance_levels(options.tier);
    std::array<VehicleSpec,2> specs;std::array<VehicleHull,2> hulls;
    const auto global=root/"Global/GLOBALB.BUN";
    for(unsigned actor=0;actor<2;++actor){
        const auto* model=find_lan_car_name(names,options.rides[actor].car);
        if(!model){error="LAN peer selected an unknown car";return false;}
        const auto& car=*model;models_[actor]=car;
        if(!load_race_vehicle_spec(global,car,levels,specs[actor],error,drift))return false;
        std::ifstream geometry(root/"Cars"/car/"GEOMETRY.BIN",std::ios::binary|std::ios::ate);
        const auto length=geometry.tellg();
        if(!geometry||length<=0||length>64*1024*1024){error="LAN "+car+" collision geometry unavailable";return false;}
        std::vector<unsigned char> bytes(static_cast<std::size_t>(length));geometry.seekg(0);
        geometry.read(reinterpret_cast<char*>(bytes.data()),length);
        if(!geometry||!decode_car_collision_hull(bytes,bin_hash(car+"_CV"),hulls[actor])){error="LAN "+car+" hull unavailable";return false;}
        CarAssets assets;if(!load_car_assets(global,car,assets,error))return false;
        if(!load_race_camera_table(global,assets,cameras_[actor],error))return false;
    }
    host_hull_=std::move(hulls[0]);
    auto world=std::make_unique<RaceSession>(course_,specs[0],RaceSteering::recovered_predicate1,host_hull_);
    world->set_player_name("PLAYER 1");
    if(!world->set_camera_table(cameras_[0],error)||(drag&&!world->set_drag(true,error,1.5F)))return false;
    RaceRivalSetup guest;guest.spec=specs[1];guest.hull=std::move(hulls[1]);guest.name="PLAYER 2";guest.car_type=models_[1];int marker=-1;
    const bool crash_cameras=split&&!drag&&!drift;
    if(crash_cameras)for(unsigned i=0;i<specs.size();++i) {
        if(!crash_roads_[i].load(root,race.slot.track,race.slot.direction,course_,specs[i].handling,nullptr,error))
            return false;
    }
    if(!vehicle_track_grid_pose(course_,1,guest.pose,marker,error))return false;
    if(drag)guest.pose.gearbox=VehicleGearbox::manual;
    if(!world->set_human_rival(guest,error,split?&cameras_[1]:nullptr)||
       (crash_cameras&&!world->set_local_crash_cameras(crash_roads_[0],crash_roads_[1],race.slot.track,error)))return false;
    if(drift&&(!world->prepare_style(root,error,event,false)||
               !world->rivals_[0]->prepare_style(root,error,event,false)))return false;
    if(drag) {
        auto local=std::make_unique<LocalDrag>();
        const std::array<const VehicleHull*,2> hull{{&host_hull_,&world->rival_setup_[0].hull}};
        for(unsigned i=0;i<2;++i) {
            const std::array<const VehicleHull*,4> selected{{nullptr,nullptr,nullptr,hull[i]}};
            if(!local->controls[i].configure(default_drag_controls_config(),error))return false;
            if(!local->planners[i].load(root,{},selected,true,race.slot.track,race.slot.direction,
                                        true,0,options.laps,course_.route.point_to_point,traffic_density,false)) {
                error="split-screen drag steering data unavailable";return false;
            }
        }
        drag_=std::move(local);
    }
    auto* world_random=drag_?drag_->planners[0].world_random():nullptr;
    if(split&&!world->set_traffic(root,traffic,error,std::max(1U,traffic_density),world_random))return false;
    if(drag_)world->rivals_[0]->world_random_=world_random;
    if(!world->start(error,&course_.start))return false;
    if(!traffic.empty()) {
        SupportedRace authored;
        if(!load_supported_race(root/"FrontEnd/FrontB.lzc",global,event,authored,error,round)||
           !world->stage_traffic_grid(authored.track.traffic_start_spacing[race.slot.direction],
                                     authored.track.traffic_start_clearance[race.slot.direction],error))return false;
    }
    session_=std::move(world);return true;
}
bool LanRaceWorld::advance(const std::array<GamepadFrame,2>& pads,std::string& error,
                           RaceSimulationProfile* profile,TimingService* timer){
    if(profile&&!timer){error="split timing service unavailable";return false;}
    if(!session_){error="LAN world is not loaded";return false;}
    if(!drag_)return session_->advance_humans(pads,error,nullptr,profile,timer);
    const auto begin=profile?timer->now_us():0;
    std::array<RaceRivalPlans,2> plans;
    for(unsigned i=0;i<2;++i) {
        auto& driver=i?session_->rival(0):*session_;
        if(session_->timing().racers[i].status!=RacerStatus::not_finished) {
            plans[i].step=driver.timing().step;
            plans[i].drag_steering=true;
            continue;
        }
        auto input=pads[i];
        input.time_us=session_->timing().step*1000000/60;
        input.sample_clock_available=false;
        drag_->controls[i].update(input);
        const auto& frame=drag_->controls[i].frame_state();
        const auto steer=frame.steer_from_button?frame.steer_digital:frame.steer_analog;
        if(!drag_->planners[i].plan(driver,plans[i],steer<0?-1:steer>0?1:0,session_.get())) {
            error="split-screen drag lane planner refused for player "+std::to_string(i+1);return false;
        }
        plans[i].gearbox_world_random=drag_->planners[0].world_random();
    }
    const double planner=profile?(timer->now_us()-begin)/1000.0:0;
    if(!session_->advance_humans(pads,error,&plans,profile,timer))return false;
    if(profile)profile->planner=planner;
    return true;
}
LanRaceFrame LanRaceWorld::snapshot()const {
    LanRaceFrame out;if(!session_)return out;
    const auto& t=session_->timing();out.step=std::uint32_t(t.step);out.world_ticks=t.step_ticks;out.race_ticks=t.race_ticks;
    out.start_ticks=t.race_start_ticks;out.countdown_start=t.countdown_start_ticks;
    out.stage=std::uint8_t(t.stage);out.outcome=std::uint8_t(t.outcome);out.countdown=std::uint8_t(t.countdown_shown);out.go=t.go_valid;
    for(unsigned i=0;i<2;++i){
        const auto& actor=i?session_->rival(0):*session_;const auto& s=actor.car();auto& c=out.cars[i];const auto& row=t.racers.at(i);
        c.position={{float(s.position.x),float(s.position.y),float(s.position.z)}};
        c.velocity={{float(s.velocity.x),float(s.velocity.y),float(s.velocity.z)}};
        c.angular_velocity={{float(s.angular_velocity.x),float(s.angular_velocity.y),float(s.angular_velocity.z)}};
        c.orientation={{float(s.orientation.w),float(s.orientation.x),float(s.orientation.y),float(s.orientation.z)}};
        for(unsigned j=0;j<4;++j){c.compression[j]=float(s.wheels[j].compression);c.wheel_phase[j]=float(actor.animation().wheel_phase[j]);}
        c.throttle=float(actor.telemetry().model.throttle);c.drive_load=actor.telemetry().model.audio_transmitted_torque;
        c.steering=float(s.steer_angle);c.rpm=std::max(0.F,float(s.engine_rpm));c.gear=s.gear;
        c.turbo=actor.turbo_pressure();
        c.parameter=actor.progress().coordinate.parameter;c.progress=actor.progress().coordinate.fraction;c.lap=row.lap;c.highest_lap=row.highest_lap;
        c.checkpoint=row.checkpoint;c.highest_checkpoint=row.highest_checkpoint;c.lap_start=row.crossing_ticks[0];
        const auto best=row.best_split_ticks[std::size_t(session_->course().route.finish)];
        c.best_lap=best==kRaceNoTime?0:best;c.finish_ticks=row.finish_ticks;c.finished=row.finish_valid;
        c.status=std::uint8_t(row.status);c.place=std::uint8_t(session_->result_positions().at(i));c.braking=actor.animation().braking;
        std::copy(std::begin(row.lap_ticks),std::end(row.lap_ticks),c.lap_ticks.begin());
        std::copy(std::begin(row.lap_total_ticks),std::end(row.lap_total_ticks),c.lap_total_ticks.begin());
        const auto nitrous=actor.nitrous();c.nitrous_capacity=nitrous.capacity;
        c.nitrous_remaining=nitrous.remaining;c.nitrous_active=nitrous.active;
    }
    return out;
}
bool LanRaceView::open(const RaceSession& source,const ChaseCameraTable& cameras,unsigned local,std::string& error,const VehicleHull* host_hull){
    if(view_||local>1||source.rival_count()!=1){error="invalid LAN render view";return false;}
    const auto& local_source=local?source.rival(0):source;
    const auto& remote_source=local?source:source.rival(0);
    const auto& guest_hull=source.rival_setup_.at(0).hull;
    const auto& first_hull=host_hull?*host_hull:guest_hull;
    auto view=std::make_unique<RaceSession>(source.course(),local_source.spec(),source.steering_mode(),local?guest_hull:first_hull);
    if(!view->set_camera_table(cameras,error))return false;
    RaceRivalSetup other;other.name="OTHER PLAYER";other.car_type=source.rival_setup_.at(0).car_type;other.spec=remote_source.spec();
    other.hull=local?first_hull:guest_hull;other.pose=source.course().start;
    if(!view->set_human_rival(other,error)||!view->start(error))return false;
    local_=local;view_=std::move(view);return true;
}
bool LanRaceView::apply(const LanRaceFrame& frame,double seconds,std::string& error,bool rear,bool camera){
    if(!view_||!std::isfinite(seconds)||seconds<=0||seconds>.25||!valid_lan_frame(frame)){
        error="invalid two-player render frame";
        return false;
    }
    auto& world=*view_;
    const auto& local=frame.cars[local_];const auto prior=world.car().position;
    const double dx=local.position[0]-prior.x,dy=local.position[1]-prior.y,dz=local.position[2]-prior.z;
    const bool reset_camera=!have_frame_||dx*dx+dy*dy+dz*dz>400;
    for(unsigned i=0;i<2;++i){
        const unsigned wire=i?1-local_:local_;const auto& c=frame.cars[wire];auto& actor=i?*world.rivals_[0]:world;
        auto& s=actor.vehicle_.state_;s.position={c.position[0],c.position[1],c.position[2]};
        s.velocity={c.velocity[0],c.velocity[1],c.velocity[2]};s.angular_velocity={c.angular_velocity[0],c.angular_velocity[1],c.angular_velocity[2]};
        double norm=0;for(float q:c.orientation)norm+=double(q)*q;norm=std::sqrt(norm);
        s.orientation={c.orientation[0]/norm,c.orientation[1]/norm,c.orientation[2]/norm,c.orientation[3]/norm};
        s.engine_rpm=c.rpm;s.engine.angular_speed=c.rpm*.10471975511966;s.gear=c.gear;s.published_gear=c.gear;s.steer_angle=c.steering;
        s.step=frame.step;s.simulation_seconds=double(frame.step)/60;
        const auto& q=s.orientation;
        s.owner_speeds.forward=float((1-2*(q.y*q.y+q.z*q.z))*s.velocity.x+2*(q.x*q.y+q.w*q.z)*s.velocity.y+2*(q.x*q.z-q.w*q.y)*s.velocity.z);
        s.owner_speeds.magnitude=float(std::sqrt(s.velocity.x*s.velocity.x+s.velocity.y*s.velocity.y+s.velocity.z*s.velocity.z));
        for(unsigned j=0;j<4;++j){s.wheels[j].compression=c.compression[j];actor.animation_.wheel_phase[j]=c.wheel_phase[j];}
        actor.animation_.braking=c.braking;actor.service_brake_=c.braking?1:0;
        actor.lan_nitrous_view_=NitrousSnapshot{c.nitrous_capacity,c.nitrous_remaining,c.nitrous_active,c.nitrous_remaining!=0};
        actor.turbo_.value=c.turbo;
        actor.vehicle_.progress_.coordinate.parameter=c.parameter;actor.vehicle_.progress_.lap=c.lap;actor.vehicle_.progress_.coordinate.fraction=c.progress;
        auto& telemetry=actor.track_telemetry_.back().model;telemetry.engine_rpm=c.rpm;telemetry.gear=c.gear;telemetry.throttle=c.throttle;telemetry.brake=c.braking?1:0;telemetry.audio_transmitted_torque=c.drive_load;
        telemetry.forward={1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y+q.w*q.z),2*(q.x*q.z-q.w*q.y)};
        telemetry.position=s.position;telemetry.velocity=s.velocity;telemetry.forward_speed=s.owner_speeds.forward;
        auto& row=world.timing_.racers[i];row.status=static_cast<RacerStatus>(c.status);row.lap=c.lap;row.highest_lap=c.highest_lap;
        // A terminal failure owns its assigned place without a timed finish.
        row.checkpoint=c.checkpoint;row.highest_checkpoint=c.highest_checkpoint;row.position=c.place;
        row.crossing_ticks[0]=c.lap_start;row.crossing_valid[0]=frame.go;row.finish_valid=c.finished;row.finish_ticks=c.finish_ticks;
        std::copy(c.lap_ticks.begin(),c.lap_ticks.end(),std::begin(row.lap_ticks));
        std::copy(c.lap_total_ticks.begin(),c.lap_total_ticks.end(),std::begin(row.lap_total_ticks));
        row.best_split_ticks[std::size_t(world.course_.route.finish)]=c.best_lap?c.best_lap:kRaceNoTime;
        actor.timing_.step=frame.step;actor.timing_.now_ticks=actor.timing_.step_ticks=frame.world_ticks;
        actor.timing_.race_ticks=frame.race_ticks;actor.timing_.race_start_ticks=frame.start_ticks;actor.timing_.race_start_valid=frame.go;
        actor.timing_.go_valid=frame.go;actor.timing_.go_ticks=frame.start_ticks;actor.timing_.countdown_valid=true;
        actor.timing_.countdown_start_ticks=frame.countdown_start;actor.timing_.countdown_shown=frame.countdown;
        actor.timing_.stage=static_cast<RaceStage>(frame.stage);actor.timing_.outcome=static_cast<RaceOutcome>(frame.outcome);
        actor.visual_frames_=frame.step;
    }
    if(frame.cars[local_].finished)world.timing_.outcome=RaceOutcome::finished;
    world.standings_.count=2;const unsigned first=frame.cars[local_].place<=frame.cars[1-local_].place?0:1;
    world.standings_.order={{first,1-first,2,3}};
    if(camera)world.chase_.next_pov();
    const auto subject=chase_subject(world.car(),world.spec());
    if(!(reset_camera?world.chase_.reset(subject,world.camera_world_,world.view_):world.chase_.update(subject,seconds,world.camera_world_,world.view_,rear))){error=world.chase_.error();return false;}
    have_frame_=true;error.clear();return true;
}
LanRaceFrame lan_interpolate(const LanRaceFrame& old,const LanRaceFrame& latest,float fraction){
    const float t=std::isfinite(fraction)?std::clamp(fraction,0.F,1.F):1.F;
    if(t>=1.F)return latest;
    // Keep countdown, results, gear and other discrete state on the same
    // timeline as the interpolated bodies. They change at the host step.
    auto result=old;
    for(unsigned i=0;i<2;++i){auto& c=result.cars[i];const auto& a=old.cars[i];const auto& b=latest.cars[i];
        float distance=0;for(unsigned j=0;j<3;++j){const float d=a.position[j]-b.position[j];distance+=d*d;}
        if(distance>400.F)continue; // Hold the old pose until the reset step.
        for(unsigned j=0;j<3;++j){c.position[j]=a.position[j]+(b.position[j]-a.position[j])*t;c.velocity[j]=a.velocity[j]+(b.velocity[j]-a.velocity[j])*t;}
        float dot=0;for(unsigned j=0;j<4;++j)dot+=a.orientation[j]*b.orientation[j];
        double norm=0;for(unsigned j=0;j<4;++j){const float start=dot<0?-a.orientation[j]:a.orientation[j];c.orientation[j]=start+(b.orientation[j]-start)*t;norm+=double(c.orientation[j])*c.orientation[j];}
        if(norm>0){const auto scale=float(1/std::sqrt(norm));for(auto& q:c.orientation)q*=scale;}
        c.steering=a.steering+(b.steering-a.steering)*t;c.rpm=a.rpm+(b.rpm-a.rpm)*t;
        for(unsigned j=0;j<4;++j){
            c.compression[j]=a.compression[j]+(b.compression[j]-a.compression[j])*t;
            // The authority wraps wheel angles at +/- pi. Linear interpolation
            // of the raw values would reverse almost one turn at that seam.
            constexpr double tau=6.28318530717958647692;
            const double turn=std::remainder(double(b.wheel_phase[j])-a.wheel_phase[j],tau);
            c.wheel_phase[j]=float(std::remainder(a.wheel_phase[j]+turn*t,tau));
        }
    }
    return result;
}

void LanSnapshotBuffer::reset_playback(double delay){
    recovery_delay_=std::clamp(delay,3.,double(entries_->size()-1));
    playing_=false;have_observed_=false;recovering_=true;
    phase_next_=phase_count_=0;delay_steps_=recovery_delay_;delay_age_steps_=0;
    gap_headroom_=0;pending_new_=false;starved_steps_=0;
}

void LanSnapshotBuffer::observe_delivery(double steps){
    delivery_intervals_[delivery_next_]=std::max(1.,steps);
    delivery_next_=(delivery_next_+1)%delivery_intervals_.size();
    delivery_count_=std::min(delivery_count_+1,delivery_intervals_.size());
    auto sorted=delivery_intervals_;
    std::sort(sorted.begin(),sorted.begin()+delivery_count_);
    // The largest repeatedly observed interval admits a variable cadence,
    // while a single isolated pause cannot redefine the expected cadence.
    normal_interval_=sorted[delivery_count_>1?delivery_count_-2:0];
}

void LanSnapshotBuffer::push(const LanRaceFrame& frame,std::uint32_t input_ack){
    if(count_&&frame.step<=at(count_-1).frame.step) {
        // The final reliable UI handoff may be retried without another world
        // step. A duplicate/older race sample still cannot move the timeline.
        if(frame.step==at(count_-1).frame.step&&frame.finish_ready&&!at(count_-1).frame.finish_ready)
            (*entries_)[(first_+count_-1)%entries_->size()]={frame,input_ack};
        return;
    }
    if(!pending_new_){prior_gap_headroom_=gap_headroom_;prior_gap_step_=last_gap_step_;}
    // A gap larger than our entire history is an outage, not clock drift.
    // Refill a short current history rather than replaying stale time or
    // increasing latency for seconds after the connection recovers.
    const auto gap=count_?frame.step-at(count_-1).frame.step:0;
    if(gap>1&&gap<entries_->size()){
        const double reserve=std::min(3.,double(gap-1));
        // Only an equally large gap renews the age of the largest reserve.
        // Continuing small losses cannot keep a one-off larger margin alive.
        if(reserve>=gap_headroom_||frame.step-last_gap_step_>=60){
            gap_headroom_=reserve;last_gap_step_=frame.step;
        }
    }
    if(count_&&gap>=entries_->size()){
        first_=(first_+count_-1)%entries_->size();count_=1;
        refilling_=true;reset_playback(delay_steps_);
    }
    if(!pending_new_){pending_first_step_=frame.step;pending_new_=true;}
    if(count_==entries_->size()){
        first_=(first_+1)%entries_->size();
        --count_;
    }
    (*entries_)[(first_+count_)%entries_->size()]={frame,input_ack};
    ++count_;
}

bool LanSnapshotBuffer::sample(double seconds,LanRaceFrame& frame,std::uint32_t& input_ack){
    if(!count_)return false;
    const double advance=std::clamp(seconds,0.,.1)*60;
    elapsed_steps_+=advance;
    fractional_age_steps_+=advance;
    // A materially fractional display phase can sample just before the next
    // group, beyond its last integer step. Keep one extra step of history.
    const double clock_advance=advance*clock_rate_;
    if(std::abs(clock_advance-std::round(clock_advance))>.01)fractional_age_steps_=0;
    if(at(count_-1).frame.finish_ready) {
        const auto& terminal=at(count_-1);frame=terminal.frame;input_ack=terminal.input_ack;
        cursor_=frame.step;playing_=true;return true;
    }
    bool isolated_pause=false;
    const double arrived=at(count_-1).frame.step;
    if(have_observed_&&arrived>observed_step_){
        const double interval=arrived-observed_step_;
        const bool outlier=interval>normal_interval_+std::max(1.,normal_interval_*.5);
        // An isolated starved receipt must not train the normal cadence.
        // Consecutive wider receipts can establish a slower delivery stream.
        isolated_pause=outlier&&!previous_pause_&&delivery_count_==delivery_intervals_.size()&&starved_steps_>.25;
        previous_pause_=outlier;
        if(!isolated_pause)observe_delivery(interval);
        if(isolated_pause){
            // A one-off silence is not evidence of a future loss cadence.
            gap_headroom_=prior_gap_headroom_;last_gap_step_=prior_gap_step_;
        }
        const double familiar_delay=std::max(3.,normal_interval_-1)+gap_headroom_+
            (fractional_age_steps_<60?1.:0.);
        const bool overwritten=interval>=entries_->size()&&cursor_+advance*rate_+.001<at(0).frame.step;
        const bool exhausted=starved_steps_>.25&&(isolated_pause||familiar_delay>delay_steps_+.5);
        if(playing_&&(overwritten||exhausted)){
            const double resume_delay=isolated_pause?delay_steps_:std::max(delay_steps_,familiar_delay);
            std::size_t old=0;
            while(old<count_&&at(old).frame.step<=observed_step_)++old;
            refilling_=old!=0;
            if(old){first_=(first_+old-1)%entries_->size();count_-=old-1;}
            reset_playback(resume_delay);
        }
    }
    if(refilling_){
        if(count_<2||at(count_-1).frame.step-at(1).frame.step<recovery_delay_){
            // Keep rendering the last authority state while a short current
            // history refills. This is a held frame, never extrapolation.
            frame=at(0).frame;input_ack=at(0).input_ack;
            pending_new_=false;++underrun_frames_;return true;
        }
        first_=(first_+1)%entries_->size();--count_;refilling_=false;
    }
    constexpr double minimum_delay=3;
    const double oldest=at(0).frame.step,newest=at(count_-1).frame.step;
    const bool received_new=!have_observed_||newest>observed_step_;
    delay_age_steps_+=advance;
    if(received_new){
        const double leading_gap=have_observed_&&pending_new_&&pending_first_step_>observed_step_?
            pending_first_step_-observed_step_-1:0;
        const double initial_span=pending_new_?newest-pending_first_step_:newest-oldest;
        if(!delivery_count_)observe_delivery(initial_span+1);
        const double span=recovering_?recovery_delay_:isolated_pause?normal_interval_-1:
            have_observed_?newest-observed_step_-1-leading_gap:initial_span;
        pending_new_=false;
        if(newest-last_gap_step_>=60)gap_headroom_=0;
        // Missing an endpoint does not lengthen the sender's group interval.
        // One recent missing step instead earns one stable step of headroom.
        const double margin=recovering_?0:gap_headroom_+(fractional_age_steps_<60?1.:0.);
        const double needed=std::min(double(entries_->size()-1),std::max(minimum_delay,span)+margin);
        if(needed>=delay_steps_){delay_steps_=needed;delay_age_steps_=0;}
        else if(delay_age_steps_>=60){delay_steps_=std::max(needed,delay_steps_-1);delay_age_steps_=0;}
        observed_step_=std::uint32_t(newest);have_observed_=true;
        phases_[phase_next_]={newest-elapsed_steps_,elapsed_steps_};
        phase_next_=(phase_next_+1)%phases_.size();phase_count_=std::min(phase_count_+1,phases_.size());
        if(phase_count_>=8){
            double earliest=elapsed_steps_;
            for(std::size_t i=0;i<phase_count_;++i)
                if(elapsed_steps_-phases_[i].at<=60)earliest=std::min(earliest,phases_[i].at);
            const double middle=(earliest+elapsed_steps_)*.5;
            const Phase* older=nullptr;const Phase* younger=nullptr;
            for(std::size_t i=0;i<phase_count_;++i){
                const auto& phase=phases_[i];if(elapsed_steps_-phase.at>60)continue;
                auto*& peak=phase.at<=middle?older:younger;
                if(!peak||phase.offset>peak->offset)peak=&phase;
            }
            if(older&&younger&&younger->at-older->at>=8){
                const double rate=1+(younger->offset-older->offset)/(younger->at-older->at);
                // Queue-age differences of a frame are not oscillator drift.
                if(rate>=.995&&rate<=1.005){
                    unsigned agreeing=0;bool above=false;
                    for(std::size_t i=0;i<phase_count_;++i){
                        const auto& phase=phases_[i];if(elapsed_steps_-phase.at>60)continue;
                        const double residual=phase.offset-older->offset-(rate-1)*(phase.at-older->at);
                        above|=residual>.002;agreeing+=std::abs(residual)<.002;
                    }
                    if(!above&&agreeing>=3)clock_rate_=rate;
                }
            }
        }
        // A packet can wait between two unequal display intervals. Its
        // newest step is then older than the current host clock. Use the
        // freshest recent clock phase, not that varying queue age. The
        // bounded recent window also expires real sender/display drift.
        clock_phase_=newest-elapsed_steps_;
        for(std::size_t i=0;i<phase_count_;++i)
            if(elapsed_steps_-phases_[i].at<=60)clock_phase_=std::max(clock_phase_,
                phases_[i].offset+(clock_rate_-1)*(elapsed_steps_-phases_[i].at));
    }
    const double target=elapsed_steps_+clock_phase_-delay_steps_;
    if(!playing_){
        if(newest-oldest<minimum_delay)return false;
        cursor_=std::clamp(target,oldest,newest);
        rate_=clock_rate_;delay_age_steps_=0;fast_until_=elapsed_steps_+60;
        playing_=true;recovering_=false;starved_steps_=0;
    }else{
        if(received_new){
            // Between receipts newest is stationary even when the sender's
            // clock is perfectly steady. Re-evaluating this error every
            // render frame would pulse playback at the packet-group cadence.
            // Startup may rebuild headroom promptly. Once playing, a noisy
            // phase estimate only trims the clock gently; it must not become
            // a visible acceleration every time the polling phase changes.
            const double correction=elapsed_steps_<fast_until_?.10:.01;
            rate_=clock_rate_+std::clamp((target-cursor_-advance*clock_rate_)*.1,-correction,correction);
        }
        double next=cursor_+advance*rate_;
        // Convergence below float pose precision must not leave discrete
        // state one step behind a body that has already rounded to that step.
        if(received_new&&target>=cursor_&&std::abs(next-target)<.001){next=target;rate_=clock_rate_;}
        if(next>newest&&at(count_-1).frame.outcome==unsigned(RaceOutcome::running))
            ++underrun_frames_;
        const double deficit=next-newest;
        if(deficit>.001)starved_steps_+=std::min(deficit,advance*clock_rate_);
        else starved_steps_=0;
        cursor_=std::clamp(next,oldest,newest);
    }
    while(count_>1&&at(1).frame.step<=cursor_){
        first_=(first_+1)%entries_->size();
        --count_;
    }
    const auto& start=at(0);
    frame=count_==1?start.frame:lan_interpolate(start.frame,at(1).frame,
        float((cursor_-start.frame.step)/(at(1).frame.step-start.frame.step)));
    input_ack=frame.step==start.frame.step?start.input_ack:at(1).input_ack;
    return true;
}
}
