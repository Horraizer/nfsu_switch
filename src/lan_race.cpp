#include "nfsu/lan_race.h"
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
    if(session_){error="LAN world already loaded";return false;}
    if(!valid_lan_options(options)){error="invalid LAN world settings";return false;}
    const auto& course=kLanCourses[options.course];SupportedRaceSelection race;
    if(!supported_race_selection(course.event,course.round,race)){error="LAN course unavailable";return false;}
    if(!load_vehicle_track_course(root/("Tracks/TRACKB"+std::to_string(race.slot.track)+".lzc"),
        options.laps,race.slot.direction,race.lap_skip,0,course_,error))return false;
    std::vector<std::string> names;
    if(!load_lan_car_names(root,names,error))return false;
    const auto levels=lan_performance_levels(options.tier);
    std::array<VehicleSpec,2> specs;std::array<VehicleHull,2> hulls;
    const auto global=root/"Global/GLOBALB.BUN";
    for(unsigned actor=0;actor<2;++actor){
        const auto* model=find_lan_car_name(names,options.rides[actor].car);
        if(!model){error="LAN peer selected an unknown car";return false;}
        const auto& car=*model;models_[actor]=car;
        if(!load_race_vehicle_spec(global,car,levels,specs[actor],error))return false;
        std::ifstream geometry(root/"Cars"/car/"GEOMETRY.BIN",std::ios::binary|std::ios::ate);
        const auto length=geometry.tellg();
        if(!geometry||length<=0||length>64*1024*1024){error="LAN "+car+" collision geometry unavailable";return false;}
        std::vector<unsigned char> bytes(static_cast<std::size_t>(length));geometry.seekg(0);
        geometry.read(reinterpret_cast<char*>(bytes.data()),length);
        if(!geometry||!decode_car_collision_hull(bytes,bin_hash(car+"_CV"),hulls[actor])){error="LAN "+car+" hull unavailable";return false;}
        CarAssets assets;if(!load_car_assets(global,car,assets,error))return false;
        std::array<unsigned char,kVehicleRecordSize> record{};std::ifstream file(global,std::ios::binary);
        file.seekg(static_cast<std::streamoff>(assets.record_offset));file.read(reinterpret_cast<char*>(record.data()),record.size());
        if(!file||!decode_chase_camera_table(record.data(),record.size(),cameras_[actor],error))return false;
    }
    host_hull_=std::move(hulls[0]);
    auto world=std::make_unique<RaceSession>(course_,specs[0],RaceSteering::recovered_predicate1,host_hull_);
    if(!world->set_camera_table(cameras_[0],error))return false;
    RaceRivalSetup guest;guest.spec=specs[1];guest.hull=std::move(hulls[1]);guest.name="GUEST";guest.car_type=models_[1];int marker=-1;
    if(!vehicle_track_grid_pose(course_,1,guest.pose,marker,error)||!world->set_human_rival(guest,error)||
       !world->start(error,&course_.start,.9F,0))return false;
    session_=std::move(world);return true;
}
bool LanRaceWorld::advance(const std::array<GamepadFrame,2>& pads,std::string& error){
    if(!session_){error="LAN world is not loaded";return false;}
    return session_->advance_humans(pads,error);
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
        c.status=std::uint8_t(row.status);c.place=std::uint8_t(session_->standings().position(i));c.braking=actor.animation().braking;
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
    LanMessage check;check.kind=LanMessageKind::snapshot;check.frame=frame;std::vector<std::uint8_t> bytes;
    if(!view_||!std::isfinite(seconds)||seconds<=0||seconds>.25||!lan_encode(check,bytes,error)){
        if(error.empty())error="invalid LAN render frame";
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
        row.checkpoint=c.checkpoint;row.highest_checkpoint=c.highest_checkpoint;row.position=c.finished?c.place:0;
        row.crossing_ticks[0]=c.lap_start;row.crossing_valid[0]=frame.go;row.finish_valid=c.finished;row.finish_ticks=c.finish_ticks;
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
        for(unsigned j=0;j<4;++j)c.compression[j]=a.compression[j]+(b.compression[j]-a.compression[j])*t;
    }
    return result;
}

void LanSnapshotBuffer::push(const LanRaceFrame& frame,std::uint32_t input_ack){
    if(count_&&frame.step<=at(count_-1).frame.step)return;
    if(count_==entries_.size()){
        first_=(first_+1)%entries_.size();
        --count_;
    }
    entries_[(first_+count_)%entries_.size()]={frame,input_ack};
    ++count_;
}

bool LanSnapshotBuffer::sample(double seconds,LanRaceFrame& frame,std::uint32_t& input_ack){
    if(!count_)return false;
    constexpr double delay_steps=3;
    const double oldest=at(0).frame.step,newest=at(count_-1).frame.step;
    if(!playing_){
        if(newest-oldest<delay_steps)return false;
        cursor_=newest-delay_steps;
        playing_=true;
    }else{
        const double advance=std::clamp(seconds,0.,.1)*60;
        // Small pacing adjustments absorb clock drift and rebuild the delay
        // after a late burst. They do not warp time by the packet gap.
        const double rate=1+std::clamp((newest-cursor_-advance-delay_steps)*.05,-.10,.10);
        if(cursor_+advance*rate>newest&&at(count_-1).frame.outcome==unsigned(RaceOutcome::running))
            ++underrun_frames_;
        cursor_=std::clamp(cursor_+advance*rate,oldest,newest);
    }
    while(count_>1&&at(1).frame.step<=cursor_){
        first_=(first_+1)%entries_.size();
        --count_;
    }
    const auto& start=at(0);
    frame=count_==1?start.frame:lan_interpolate(start.frame,at(1).frame,
        float((cursor_-start.frame.step)/(at(1).frame.step-start.frame.step)));
    input_ack=frame.step==start.frame.step?start.input_ack:at(1).input_ack;
    return true;
}
}
