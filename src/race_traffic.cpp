#include "nfsu/race_traffic.h"
#include "nfsu/ai_path_choice.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/car_assets.h"
#include "nfsu/ai_traffic_action.h"
#include "nfsu/race_setup.h"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace nfsu {
namespace {
constexpr double tau=6.2831853071795864769;
float distance2(const VehicleVec3& a,const VehicleVec3& b) {
    return float((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y));
}
AiDriverPoint lane_point(const RoutePoint& p,unsigned lane) {
    const auto* direction=lane<6?p.forward:p.backward;
    const float offset=p.lane_offset[lane];
    return {{p.position[0]+direction[1]*offset,p.position[1]-direction[0]*offset,
        p.height+p.unknown_2c*offset}};
}
bool publish(RaceTrafficActor& a,float height,float dt,bool count_step=true) {
    auto& s=a.car;const double angle=a.motion.heading,c=std::cos(angle),sn=std::sin(angle);
    const double w=std::cos(angle*.5),z=std::sin(angle*.5);const auto& p=a.initial_pitch;
    s.orientation={w*p.w,w*p.x-z*p.y,w*p.y+z*p.x,z*p.w};
    const std::array<float,3> previous{{float(s.position.x),float(s.position.y),float(s.position.z)}};
    const std::array<float,3> current{{a.motion.x,a.motion.y,height+a.body_height}};
    TrafficBodyPublication published;
    if(dt>0) {
        if(!traffic_motion_publish(previous,current,a.motion.heading,a.motion.speed,dt,a.published_heading,published))return false;
    } else {
        // Spawn has no completed movement interval; retain the placement adapter.
        published.velocity={float(c*a.motion.speed),float(sn*a.motion.speed),0};
        published.forward=published.magnitude=a.motion.speed;
        published.heading=std::uint16_t(std::int32_t(a.motion.heading*10430.3779296875F));
    }
    a.published_heading=published.heading;s.position={current[0],current[1],current[2]};
    s.velocity={published.velocity[0],published.velocity[1],published.velocity[2]};
    s.owner_speeds={published.forward,published.magnitude};
    s.angular_velocity={0,0,published.yaw_rate};s.rigid_orientation=s.orientation;s.rigid_body_ready=true;
    auto& b=s.rigid_body;b.position={{float(s.position.x),float(s.position.y),float(s.position.z)}};
    b.velocity=published.velocity;
    for(unsigned k=0;k<3;++k)b.momentum[k]=b.velocity[k]*a.setup.spec.handling.mass;
    b.angular_velocity={{0,0,published.yaw_rate}};b.angular_momentum={};
    // Collision adapter builds inertia axes from the public quaternion.
    const auto& q=s.orientation;
    b.axes={{{{float(1-2*(q.y*q.y+q.z*q.z)),float(2*(q.x*q.y+q.w*q.z)),float(2*(q.x*q.z-q.w*q.y))}},
              {{float(2*(q.x*q.y-q.w*q.z)),float(1-2*(q.x*q.x+q.z*q.z)),float(2*(q.y*q.z+q.w*q.x))}},
              {{float(2*(q.x*q.z+q.w*q.y)),float(2*(q.y*q.z-q.w*q.x)),float(1-2*(q.x*q.x+q.y*q.y))}}}};
    b.snapshot_axes=b.axes;
    s.steering_word=a.driver.steering.word;s.steering_word_ready=true;
    s.steer_angle=double(static_cast<std::int16_t>(s.steering_word))*tau/65536;
    for(unsigned k=0;k<4;++k) {
        s.wheels[k].spin=a.motion.speed/a.setup.spec.handling.wheels[k].radius;
        s.wheels[k].contact=true;
        a.animation.wheel_phase[k]=std::fmod(a.animation.wheel_phase[k]+s.wheels[k].spin*dt,tau);
    }
    a.animation.braking=a.driver.pedals.brake>0;
    if(count_step){++s.step;s.simulation_seconds+=dt;}return true;
}
}
#include "race_traffic_actions.inc"
bool RaceTraffic::load(const std::filesystem::path& root,const VehicleTrackCourse& course,
                       const std::vector<RaceTrafficSetup>& setup,std::string& error,unsigned density,std::uint32_t* random,bool drag) {
    if(course_||setup.empty()||setup.size()>kRaceTrafficCapacity){error="invalid traffic pool";return false;}
    if(density<1||density>3){error="invalid traffic density selector";return false;}
    density_=density;world_random_=random;drag_=drag;
    std::vector<unsigned char> image;
    const auto track=load_race_route_track(root/"Tracks"/course.name,image);
    if(!track.ok()||!parse_racing_line_image(image.data(),image.size(),{},lines_)) {
        error="traffic road data unavailable";return false;
    }
    try {
        const auto marker=course.name.find("TRACKB");
        if(marker==std::string::npos)throw std::invalid_argument("track name");
        track_number_=unsigned(std::stoul(course.name.substr(marker+6)));
    }catch(...){error="traffic track identity unavailable";return false;}
    auto u32=[](const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);};
    for(std::size_t p=0;p+8<=image.size();) {
        const auto id=u32(image.data()+p),n=u32(image.data()+p+4);p+=8;
        if(n>image.size()-p){error="traffic road chunks truncated";return false;}
        if(id==0x34122) {
            if(!ai_road_events(image.data()+p,n,events_)){error="traffic road events invalid";return false;}

        }else if(id==0x34123) {
            if(!ai_traffic_intersections(image.data()+p,n,intersections_)){error="traffic intersections invalid";return false;}
        }
        p+=n;
    }
    if(!ai_road_initialize(course.track,lines_,events_,error))return false;
    backward_lines_=lines_;
    if(!ai_road_orient(backward_lines_,1)){error="traffic reverse roads refused";return false;}
    for(const auto& r:lines_.routes)route_reverse_[0][unsigned(r.index)]=r.reverse;
    for(const auto& r:backward_lines_.routes)route_reverse_[1][unsigned(r.index)]=r.reverse;
    const auto types=load_car_type_table(root/"Global/GLOBALB.BUN");
    std::ifstream records(root/"Global/GLOBALB.BUN",std::ios::binary);
    if(!types.ok()||!records){error="traffic steering records unavailable";return false;}
    actors_.reserve(setup.size());models_.reserve(setup.size());impact_sessions_.reserve(setup.size());
    for(const auto& entry:setup) {
        if(entry.hull.points.empty()){error="traffic hull absent";return false;}
        auto model=std::make_unique<VehicleModel>(entry.spec);
        RaceTrafficActor actor;actor.setup=entry;actor.box=race_contact_box(entry.hull);actor.bounds=race_track_body_bounds(entry.spec,actor.box);
        const auto type=find_car_type(types,entry.name);
        if(type==kNoCarType){error="traffic steering record absent: "+entry.name;return false;}
        // 45F6E2..F5 reads setup+270 = CarTypeInfo+380. Record+270 is
        // the unrelated alternate yaw inertia multiplier, not steering.
        unsigned char raw_scale[4]{};
        records.seekg(static_cast<std::streamoff>(types.records[type].offset+0x380));
        records.read(reinterpret_cast<char*>(raw_scale),4);
        const auto scale_bits=u32(raw_scale);
        std::memcpy(&actor.steering_scale,&scale_bits,4);
        if(!records||!std::isfinite(actor.steering_scale)) {
            error="traffic steering record invalid: "+entry.name;return false;
        }
        VehiclePose pose;
        if(!model->reset(actor.car,pose)){error="traffic model refused: "+entry.name;return false;}
        actor.body_height=float(actor.car.position.z);actor.initial_pitch=actor.car.orientation;
        auto impact=std::make_unique<VehicleTrackSession>(*model,course,VehicleTrackApproximations{},entry.hull);
        float rating=0,low_slip=0;
        if(!ai_setup_performance(entry.spec.handling,rating,low_slip)||!impact->configure_ai(low_slip)) {
            error="traffic selector3 setup refused: "+entry.name;return false;
        }
        actor.ride_category=race_performance_category(rating);
        actors_.push_back(std::move(actor));models_.push_back(std::move(model));
        impact_sessions_.push_back(std::move(impact));
    }
    course_=&course;error.clear();return true;
}
bool RaceTraffic::world_prediction(unsigned index,const AiPathActorSnapshot& actor,
                                   std::uint32_t tick,float horizon,AiPlannedPath& out)const {
    if(index>=actors_.size()||!actors_[index].active)return false;
    const auto& a=actors_[index];float outside=0;
    if(a.road.selected) {
        const auto* route=(a.lane>=6?backward_lines_:lines_).find(a.road.road.route);
        if(!route||a.road.road.point>=route->points.size())return false;
        const auto& p=route->points[a.road.road.point];
        //44ACB0 uses the game x87 control word007F (24-bit arithmetic).
        const float lateral=(actor.position[0]-p.position[0])*p.forward[1]-
                            (actor.position[1]-p.position[1])*p.forward[0];
        outside=lateral>0?lateral-p.unknown_28:-(lateral+p.unknown_24);
    }
    return ai_traffic_world_prediction(actor,a.driver_path,outside,a.action_owner.current.kind,tick,horizon,out);
}
bool RaceTraffic::spawn(unsigned index,const std::array<const VehicleState*,4>& racers,std::uint64_t tick,std::string& error) {
    auto& a=actors_[index];const auto& player=*racers[0];
    auto& random=world_random_?*world_random_:rng_;
    const float reference[2]={float(player.position.x),float(player.position.y)};
    const auto& track=course_->track;
    const int zone_index=track_path_zone_index(track,4,course_->route.reverse?1:0,reference);
    const auto* zone=zone_index<0?nullptr:&track.zones[std::size_t(zone_index)];
    TrackPathCoordinate coordinate;
    update_track_path_coordinate(course_->route.spline,track,coordinate,reference);
    const float raw_progress=course_->route.reverse?course_->route.spline.path_length-coordinate.distance:coordinate.distance;
    const auto difference=[&](float left,float right) {
        float value=left-right;const float length=course_->route.spline.path_length;
        //443D60 wraps only closed tracks, once, with strict half-span tests.
        if(!course_->route.spline.open) {
            if(value<length*-.5F)value+=length;
            else if(value>length*.5F)value-=length;
        }
        return value;
    };
    bool stopped=false;
    for(const auto& other:actors_)if(other.active&&other.car.owner_speeds.magnitude<1&&
       std::abs(difference(other.progress,raw_progress))<250)stopped=true;
    float spacing;
    if(!ai_traffic_spawn_spacing(zone,density_,stopped,random,spacing))return false;
    // Port admission uses authored roads only. No nearest-spline teleporting.
    // Original ordinary teleporter uses a250..500m horizon and one pool slot/s.
    const auto sample=random%0x7fffffffU;const auto x=random^0x1d872b41U;
    const auto y=(x>>5)^x;random=(y<<27)^y^x;
    const unsigned start=sample%lines_.routes.size();
    for(unsigned ri=0;ri<lines_.routes.size();++ri) {
        const auto& route=lines_.routes[(start+ri)%lines_.routes.size()];
        //599B20 searches the native drag road table slots0..98 only.
        //The ordinary teleporter may use other authored traffic routes.
        if(drag_&&(route.index<0||route.index>=99))continue;
        for(unsigned li=0;li<12;++li) {
            const unsigned lane=(li+index)%12;if(!route.lane_enabled[lane])continue;
            for(unsigned pi=1;pi+1<route.points.size();++pi) {
                //599C40 requires ten authored points in the travel direction.
                if(drag_&&!ai_traffic_drag_spawn_margin(pi,unsigned(route.points.size()),lane<6))continue;
                //5999C0/599B20 reject points denied by4420B0 before placement.
                //Enabled race lanes are not necessarily traffic spawn lanes.
                if(!ai_traffic_spawn_point_allowed(route.points[pi].unknown_84,lane<6))continue;
                auto cursor=AiRoadCursor{route.index,std::uint16_t(pi)};
                const auto* oriented=(lane>=6?backward_lines_:lines_).find(route.index);
                if(!oriented||pi>=oriented->points.size())continue;
                const auto& authored=oriented->points[pi];
                const auto point=lane_point(authored,lane%6);
                VehicleVec3 position{point[0],point[1],point[2]};
                const auto dist=distance2(position,player.position);
                if(dist<250.F*250.F||dist>500.F*500.F)continue;
                bool clear=true;
                for(const auto* r:racers)if(r&&distance2(position,r->position)<250.F*250.F)clear=false;
                for(const auto& other:actors_)if(other.active&&
                    ai_traffic_spawn_gap_blocked(difference(route.points[pi].path_distance,other.progress),spacing,true))clear=false;
                if(!clear)continue;
                a.cursor=cursor;a.lane=lane;a.road={};a.road.selected=true;a.road.road=cursor;
                a.road.category=11+int(lane%6);a.road_policy={};a.published_path={};a.driver_path={};a.maneuver={};
                a.action_owner={};a.action_records={};a.action_speed={};a.next_action_handle=1;
                a.actor_override_enabled=false;a.actor_override={};a.coordinate={};a.ended=false;
                a.target_speed=std::min(24.58650016784668F,float(authored.speed[11+lane%6]));
                const auto* forward=authored.forward;
                a.motion={point[0],point[1],float(std::atan2(forward[1],forward[0])),a.target_speed};
                // Recycled bodies must not inherit impact compression/rest/contact caches.
                VehiclePose pose;
                if(!models_[index]->reset(a.car,pose)){error="traffic spawn model refused";return false;}
                a.driver={};a.animation={};a.impacted=false;a.crash_stop=false;
                a.pair_history={};a.active=true;a.registration=spawns+1;
                if(!publish(a,point[2],0,false)||!prepare_world(error)||!plan(index,std::uint32_t(world_clock_>=0?world_clock_:tick*4000/60),error)) {
                    if(error.empty())error="traffic spawn controller refused";
                    return false;
                }
                if(!a.active)continue;
                a.motion.speed=a.target_speed;
                if(!publish(a,point[2],0)||!impact_sessions_[index]->start(a.car)){
                    error="traffic spawn publication refused";return false;
                }
                a.previous_valid=false;++a.activations;++spawns;return true;
            }
        }
    }
    return false;
}
void RaceTraffic::contact(RaceTrafficActor& actor,unsigned other,const ContactVector& impulse,std::uint64_t tick,const ContactRecord* shared_record) {
    actor.impacted=true;
    ++actor.contacts;
    if(other>=actor.pair_history.size())return;
    auto& record=actor.pair_history[other];
    // The existing world runs60Hz; contact records expire after .3s without a
    // hit (420720). Keep each other body separate, not a lifetime car sum.
    if(!record.valid||tick-record.last_tick>18)record.magnitude=0;
    const float x=impulse[0]*impulse[0],z=impulse[2]*impulse[2],y=impulse[1]*impulse[1];
    record.magnitude+=float(vehicle_sqrt(double((x+z)+y)));
    record.last_tick=tick;record.valid=true;
    if(shared_record)record.magnitude=shared_record->magnitude_sum;
    if(record.magnitude>30.F)actor.crash_stop=true;
}
void RaceTraffic::defer_body_contacts(bool value) {
    for(auto& session:impact_sessions_)session->defer_body_contacts(value);
}
bool RaceTraffic::prepare_contact(unsigned index,std::string& error,bool& activated) {
    activated=false;
    if(index>=actors_.size()){error="invalid traffic contact activation";return false;}
    auto& actor=actors_[index];
    if(actor.impacted)return true;
    //46B5F4/46B600 call469FB0 before either body's time rewind. The
    //selector2->3 factory initializes struts at the published endpoint and
    //leaves the shared component interval at zero.
    //426110 constructs the new rigid component from published mover V/W;
    //the kinematic placeholder has no retained physical angular momentum.
    actor.car.rigid_body_ready=false;
    const auto published=actor.car;
    if(!refresh_contact(index,actor.car,error,&published,0))return false;
    actor.impacted=true;activated=true;return true;
}
bool RaceTraffic::refresh_contact(unsigned index,VehicleState& sampled,std::string& error,const VehicleState* published_actor,float interval) {
    if(index>=actors_.size()){error="invalid traffic contact refresh";return false;}
    const auto& actor=actors_[index];VehicleInput controls;
    controls.world_frame_seconds=frame_seconds_;
    controls.world_clock_valid=world_clock_>=0;controls.world_clock=world_clock_;
    controls.throttle=actor.driver.pedals.throttle;controls.brake=actor.driver.pedals.brake;
    controls.road_steering=true;controls.road_angle_word_valid=true;
    controls.road_angle_word=actor.driver.steering.word;
    VehicleTrackTelemetry telemetry;
    if(!impact_sessions_[index]->step_ai_body(sampled,controls,telemetry,0,published_actor,true,false,interval,actor.driver.gear_request)) {
        error="traffic contact refresh: "+impact_sessions_[index]->error();return false;
    }
    return true;
}
bool RaceTraffic::continue_contact(unsigned index,double seconds,std::string& error,const VehicleState* published_actor,bool refreshed) {
    if(index>=actors_.size()||!std::isfinite(seconds)||seconds<0){error="invalid traffic continuation";return false;}
    auto& actor=actors_[index];VehicleInput controls;
    controls.world_frame_seconds=frame_seconds_;
    controls.world_clock_valid=world_clock_>=0;controls.world_clock=world_clock_;
    controls.throttle=actor.driver.pedals.throttle;controls.brake=actor.driver.pedals.brake;
    controls.road_steering=true;controls.road_angle_word_valid=true;
    controls.road_angle_word=actor.driver.steering.word;
    VehicleTrackTelemetry telemetry;
    if(!impact_sessions_[index]->step_ai_body(actor.car,controls,telemetry,float(seconds),published_actor,false,refreshed,0,actor.driver.gear_request)) {
        error="traffic contact continuation: "+impact_sessions_[index]->error();return false;
    }
    return true;
}
bool RaceTraffic::prepare_frame(const std::array<const VehicleState*,4>& racers,std::uint64_t tick,std::string& error,std::int32_t world_clock,float dt,const std::array<bool,4>& ghosts,const std::array<const RaceContactBox*,4>& racer_shapes) {
    if(!std::isfinite(dt)||dt<=0||dt>.1F){error="invalid traffic frame interval";return false;}
    if(frame_prepared_){error="traffic frame already prepared";return false;}
    frame_seconds_=dt;world_clock_=world_clock;
    last_component_interval=-1;
    if(!course_||!racers[0]){error="traffic owner missing";return false;}
    error.clear();racers_=racers;racer_shapes_=racer_shapes;racer_ghosts_=ghosts;
    for(auto& table:intersections_)if(!ai_traffic_intersection_step(table)){
        error="traffic intersection phase refused";return false;
    }
    for(auto& a:actors_){a.previous_pose=race_contact_pose(a.car);a.previous_valid=a.active;}
    if(tick%60==0) {
        auto& a=actors_[recycle_];bool near=false;
        for(const auto* r:racers)if(r&&distance2(a.car.position,r->position)<250.F*250.F)near=true;
        if(a.active&&!near&&!retire(recycle_,error))return false;
        if(!a.active&&!spawn(recycle_,racers,tick,error)&&!error.empty())return false;
        recycle_=(recycle_+1)%actors_.size();
    }
    if(!prepare_world(error))return false;
    auto& registered=prepared_actors_;prepared_count_=0;auto& count=prepared_count_;
    for(unsigned i=0;i<actors_.size();++i)if(actors_[i].active)registered[count++]=i;
    std::sort(registered.begin(),registered.begin()+count,[&](unsigned a,unsigned b){return actors_[a].registration<actors_[b].registration;});
    for(unsigned slot=0;slot<count;++slot) {
        const unsigned i=registered[slot];auto& a=actors_[i];if(!a.active)continue;
        if(a.impacted) {
            // Read the physical pose back into the planner; never publish the
            // old kinematic pose over an impulse or an airborne body.
            const auto& axis=a.car.rigid_body.axes[0];
            a.motion.x=float(a.car.position.x);a.motion.y=float(a.car.position.y);
            a.motion.heading=std::atan2(axis[1],axis[0]);
            a.motion.speed=float(a.car.velocity.x*axis[0]+a.car.velocity.y*axis[1]);
        }
        bool full=false;
        if(!ai_traffic_full_update(std::uint32_t(tick),slot,count,full)){error="traffic scheduler refused";return false;}
        if(full&&!a.crash_stop) {
            if(!plan(i,std::uint32_t(world_clock_>=0?world_clock_:tick*4000/60),error))return false;
            if(!a.active)continue;
        }
    }
    // All controller publications are complete before any Driver is stepped.
    for(unsigned slot=0;slot<count;++slot) {
        const unsigned i=registered[slot];auto& a=actors_[i];if(!a.active)continue;
        const float desired=a.crash_stop?0:a.target_speed;
        AiDriverForwardInput input;input.path=a.crash_stop?AiDriverPath{}:a.path;input.category=2;
        input.position={{float(a.car.position.x),float(a.car.position.y),float(a.car.position.z)}};
        input.velocity={{float(a.car.velocity.x),float(a.car.velocity.y),float(a.car.velocity.z)}};
        input.heading=std::uint16_t(std::int32_t(a.motion.heading*65536/tau));
        input.forward_speed=a.impacted?a.car.owner_speeds.forward:a.motion.speed;
        input.absolute_speed=a.impacted?a.car.owner_speeds.magnitude:std::abs(a.motion.speed);
        input.desired_speed=desired;input.rear_drive_fraction=a.setup.spec.handling.rear_drive_fraction;
        for(unsigned k=0;k<4;++k) {
            input.contact[k]=a.impacted?a.car.wheels[k].contact:true;
            input.wheel_slip[k]=a.car.wheels[k].prepared_slip;
        }
        if(a.impacted) {
            const auto& axis=a.car.rigid_body.axes[0];
            input.heading=track_angle(axis[0],axis[1]);
        }
        const bool driver_ok=a.impacted?
            ai_driver_traffic_impact_step(input,dt,a.driver,a.steering_scale):
            ai_driver_traffic_step(input,dt,a.driver,a.steering_scale);
        if(!driver_ok) {
            error="traffic driver refused";return false;
        }
    }
    advanced_actors_.fill(false);frame_prepared_=true;
    error.clear();return true;
}
bool RaceTraffic::advance_body(unsigned i,std::string& error) {
    if(!frame_prepared_||i>=actors_.size()||advanced_actors_[i]) {
        error="traffic physical phase not prepared or already consumed";return false;
    }
    advanced_actors_[i]=true;auto& a=actors_[i];
    if(!a.active)return true;
    const float dt=frame_seconds_;
    if(a.impacted) {
        VehicleInput controls;
        controls.world_frame_seconds=frame_seconds_;
        controls.world_clock_valid=world_clock_>=0;controls.world_clock=world_clock_;
        controls.throttle=a.driver.pedals.throttle;
        controls.brake=a.driver.pedals.brake;
        controls.road_steering=true;controls.road_angle_word_valid=true;
        controls.road_angle_word=a.driver.steering.word;
        VehicleTrackTelemetry telemetry;
        last_component_interval=dt/float(unsigned(dt/.00839999970048666F)+1);
        if(!impact_sessions_[i]->step_ai_body(a.car,controls,telemetry,-1,nullptr,false,false,0,a.driver.gear_request)) {
            error="traffic impact body refused: "+impact_sessions_[i]->error();return false;
        }
        for(unsigned wheel=0;wheel<4;++wheel)
            a.animation.wheel_phase[wheel]=std::fmod(a.animation.wheel_phase[wheel]+(dt==1.F/60.F?a.car.wheels[wheel].spin/60:a.car.wheels[wheel].spin*double(dt)),tau);
        a.animation.braking=controls.brake>0;
        error.clear();return true;
    }
    const auto& h=a.setup.spec.handling;
    const auto previous_motion=a.motion;
    TrafficMotionInput motion;motion.front_axle=h.wheels[0].longitudinal;motion.rear_axle=h.wheels[2].longitudinal;
    motion.throttle=a.driver.pedals.throttle;motion.brake=a.driver.pedals.brake;
    motion.dt=dt;motion.steering=a.driver.steering.word;
    if(!traffic_motion_step(motion,a.motion)){error="traffic mover refused";return false;}
    a.motion.heading=std::remainder(a.motion.heading,float(tau));
    const auto& road=lines_.find(a.cursor.route)->points[a.cursor.point];
    const float point[3]={a.motion.x,a.motion.y,road.height+2};TrackContact hit;
    if(query_track_contact_point(course_->world,point,2,8,hit)!=TrackContactStatus::hit) {
        // Missing road is a stop, never a reset visible in front of a racer.
        a.motion=previous_motion;a.motion.speed=0;a.target_speed=0;error.clear();return true;
    }
    if(!publish(a,hit.point[2],dt)){error="traffic publication refused";return false;}
    error.clear();return true;
}
void RaceTraffic::finish_frame() {frame_prepared_=false;prepared_count_=0;}
bool RaceTraffic::advance(const std::array<const VehicleState*,4>& racers,std::uint64_t tick,std::string& error,std::int32_t world_clock,float dt,const std::array<bool,4>& ghosts,const std::array<const RaceContactBox*,4>& racer_shapes) {
    if(!prepare_frame(racers,tick,error,world_clock,dt,ghosts,racer_shapes))return false;
    for(unsigned slot=0;slot<prepared_count_;++slot)
        if(!advance_body(prepared_actors_[slot],error)){finish_frame();return false;}
    finish_frame();error.clear();return true;
}
}
