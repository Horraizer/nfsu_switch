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
#include <limits>
namespace nfsu {
namespace {
constexpr float kOrdinaryTrafficSpeedCap=24.58650016784668F;
constexpr double tau=6.2831853071795864769;
float distance2(const VehicleVec3& a,const VehicleVec3& b) {
    return float((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y));
}
float path_difference(float left,float right,const TrackPathSpline& spline) {
    float value=left-right;const float length=spline.path_length;
    //443D60 wraps only closed tracks, once, with strict half-span tests.
    if(!spline.open) {
        if(value<length*-.5F)value+=length;
        else if(value>length*.5F)value-=length;
    }
    return value;
}
std::uint32_t traffic_random(std::uint32_t& seed) {
    const auto sample=seed;
    const auto x=seed^0x1d872b41U,y=(x>>5)^x;
    seed=(y<<27)^y^x;
    return sample;
}
float traffic_random_unit(std::uint32_t& seed) {
    return float(traffic_random(seed)%0x7fffffffU)*4.656612873077393e-10F;
}
int traffic_grid_lane(const Route& route,bool forward,std::uint32_t& random) {
    const unsigned first=forward?0:6;
    for(unsigned attempt=0;attempt<10;++attempt) {
        const unsigned lane=first+(traffic_random(random)%600000U)/100000U;
        if(route.lane_enabled[lane])return int(lane);
    }
    for(unsigned lane=first;lane<first+6;++lane) {
        if(route.lane_enabled[lane])return int(lane);
    }
    return -1;
}
AiDriverPoint lane_point(const RoutePoint& p,unsigned lane) {
    const auto* direction=lane<6?p.forward:p.backward;
    const float offset=p.lane_offset[lane];
    return {{p.position[0]+direction[1]*offset,p.position[1]-direction[0]*offset,
        p.height+p.unknown_2c*offset}};
}
// 442590 converts reverse lane offsets to the raw forward coordinate.
float placement_lateral(const RoutePoint& point,unsigned lane) {
    const float offset=point.lane_offset[lane];
    return lane<6?offset:offset*(point.backward[0]*point.forward[0]+
                                 point.backward[1]*point.forward[1]);
}
AiDriverPoint placement_point(const RoutePoint& point,unsigned lane,float longitudinal) {
    const float lateral=placement_lateral(point,lane);
    return {{point.position[0]+point.forward[1]*lateral+point.forward[0]*longitudinal,
             point.position[1]-point.forward[0]*lateral+point.forward[1]*longitudinal,
             point.height+point.unknown_2c*lateral+point.unknown_30*longitudinal}};
}
bool publish(RaceTrafficActor& a,const VehicleTrackSession& session,float reference_z,
             float dt,std::uint16_t ground_heading,bool fresh=false,bool count_step=true) {
    auto& s=a.car;
    const std::array<float,3> previous{{float(s.position.x),float(s.position.y),float(s.position.z)}};
    VehicleVec3 position{a.motion.x,a.motion.y,reference_z};
    PlayerRigidMatrix axes;
    std::array<VehicleTrackCast,kVehicleWheelCount> casts;
    session.fit_road_pose(ground_heading,position,axes,casts,fresh);
    if(std::any_of(casts.begin(),casts.end(),[](const auto& cast){
        return cast.surface==VehicleTrackSurface::refused;
    }))return false;
    const std::array<float,3> current{{a.motion.x,a.motion.y,float(position.z)}};
    TrafficBodyPublication published;
    if(dt>0) {
        if(!traffic_motion_publish(previous,current,a.motion.heading,a.motion.speed,dt,a.published_heading,published))return false;
    } else {
        // 427C50 publishes the fitted forward axis at the requested speed + 0.1.
        const float lead=a.motion.speed+float(kVehicleTrackResetSpeedLead);
        for(unsigned k=0;k<3;++k)published.velocity[k]=axes[0][k]*lead;
        published.forward=a.motion.speed;
        published.magnitude=std::hypot(published.velocity[0],published.velocity[1],published.velocity[2]);
        published.heading=ground_heading;
    }
    a.published_heading=published.heading;
    s.position=position;s.orientation=vehicle_orientation_from_axes(axes);
    s.dynamic_ground_candidate=casts.back().hit.dynamic_candidate;
    s.velocity={published.velocity[0],published.velocity[1],published.velocity[2]};
    s.owner_speeds={published.forward,published.magnitude};
    s.angular_velocity={0,0,published.yaw_rate};s.rigid_orientation=s.orientation;s.rigid_body_ready=true;
    auto& b=s.rigid_body;b.position={{float(s.position.x),float(s.position.y),float(s.position.z)}};
    b.velocity=published.velocity;
    for(unsigned k=0;k<3;++k)b.momentum[k]=b.velocity[k]*a.setup.spec.handling.mass;
    b.angular_velocity={{0,0,published.yaw_rate}};b.angular_momentum={};
    b.axes=b.snapshot_axes=axes;
    player_publish_owner_motion(s.owner_motion,b.velocity,b.angular_velocity,axes);
    s.owner_motion.heading=published.heading;
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
    auto& world_seed=world_random_?*world_random_:rng_;
    auto placement_seed=world_seed;
    std::size_t spawn_count=0;
    for(std::size_t p=0;p+8<=image.size();) {
        const auto id=u32(image.data()+p),n=u32(image.data()+p+4);p+=8;
        if(n>image.size()-p){error="traffic road chunks truncated";return false;}
        if(id==0x34122) {
            if(!ai_road_events(image.data()+p,n,events_)){error="traffic road events invalid";return false;}

        }else if(id==0x34123) {
            if(!ai_traffic_intersections(image.data()+p,n,intersections_)){error="traffic intersections invalid";return false;}
        }else if(id==0x34124) {
            constexpr std::size_t record_size=0x108;
            if(n%record_size||n/record_size+spawn_count>20) {
                error="invalid traffic placement records";return false;
            }
            spawn_count+=n/record_size;
            const unsigned reverse=unsigned(course.route.reverse);
            const unsigned value_offset=8*density_+4*reverse;
            const auto read_float=[&](const unsigned char* bytes) {
                const auto bits=u32(bytes);float value;
                std::memcpy(&value,&bits,sizeof(value));
                return value;
            };
            for(std::size_t i=0;i<n;i+=record_size) {
                const auto* record=image.data()+p+i;
                // 598440 initializes every row before the actor factory,
                // even if the current direction/density cannot use it.
                std::array<float,8> base{},spread{},far_scale{};
                for(unsigned j=0;j<base.size();++j) {
                    base[j]=read_float(record+0x30+4*j);
                    spread[j]=read_float(record+0x50+4*j);
                }
                ai_traffic_placement_ranges(base,spread,placement_seed,far_scale);
                if(std::int8_t(record[0x100])>=6)continue;
                const auto id=u32(record);
                if(id>kRouteMaxIndex)continue;
                SpawnRecord spawn{};
                spawn.road=std::int16_t(id);
                for(unsigned direction=0;direction<2;++direction) {
                    const unsigned offset=4+2*direction;
                    spawn.longitudinal_offsets[direction]=read_float(record+8+4*direction);
                    if(!std::isfinite(spawn.longitudinal_offsets[direction])) {
                        error="invalid traffic placement offset";return false;
                    }
                    spawn.exit_points[direction]=std::int16_t(unsigned(record[offset])|(unsigned(record[offset+1])<<8));
                    spawn.points[direction]=record[0xd0+4*density_+2*direction+reverse]?spawn.exit_points[direction]:-1;
                }
                if(spawn.points[0]<0&&spawn.points[1]<0)continue;
                spawn.speed_limit=read_float(record+0xb0+value_offset)*0.4470272660255432F;
                spawn.near_scale=read_float(record+0x10+value_offset);
                spawn.far_scale=far_scale[value_offset/4];
                spawn.gap_min=read_float(record+0x70+value_offset);
                spawn.gap_max=read_float(record+0x90+value_offset);
                if(!std::isfinite(spawn.speed_limit)||spawn.speed_limit<0||
                   !std::isfinite(spawn.near_scale)||!std::isfinite(spawn.far_scale)||
                   !std::isfinite(spawn.gap_min)||!std::isfinite(spawn.gap_max)) {
                    error="invalid traffic placement values";return false;
                }
                spawn_records_.push_back(spawn);
            }
        }
        p+=n;
    }
    has_spawn_table_=spawn_count!=0;
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
        unsigned char appearance_count{};
        unsigned char category[4]{};
        const auto offset=types.records[type].offset;
        records.seekg(static_cast<std::streamoff>(offset+0xd9));
        records.read(reinterpret_cast<char*>(&appearance_count),1);
        records.seekg(static_cast<std::streamoff>(offset+0xc54));
        records.read(reinterpret_cast<char*>(category),4);
        const int variants=static_cast<std::int8_t>(appearance_count);
        if(!records||variants<0||variants>16) {
            error="traffic appearance record invalid";return false;
        }
        actor.appearance_variants=u32(category)==1?1U:unsigned(variants);
        records.seekg(static_cast<std::streamoff>(offset+0x380));
        records.read(reinterpret_cast<char*>(raw_scale),4);
        const auto scale_bits=u32(raw_scale);
        std::memcpy(&actor.steering_scale,&scale_bits,4);
        if(!records||!std::isfinite(actor.steering_scale)) {
            error="traffic steering record invalid: "+entry.name;return false;
        }
        VehiclePose pose;
        if(!model->reset(actor.car,pose)){error="traffic model refused: "+entry.name;return false;}
        auto impact=std::make_unique<VehicleTrackSession>(*model,course,VehicleTrackApproximations{},entry.hull);
        float rating=0,low_slip=0;
        if(!ai_setup_performance(entry.spec.handling,rating,low_slip)||!impact->configure_ai(low_slip)) {
            error="traffic selector3 setup refused: "+entry.name;return false;
        }
        actor.ride_category=race_performance_category(rating);
        actors_.push_back(std::move(actor));models_.push_back(std::move(model));
        impact_sessions_.push_back(std::move(impact));
    }
    world_seed=placement_seed;course_=&course;error.clear();return true;
}
bool RaceTraffic::actor_recovering(unsigned index,std::int32_t clock)const {
    return index<impact_sessions_.size()&&actors_[index].impacted&&
        impact_sessions_[index]->owner_recovery_active(clock);
}
bool RaceTraffic::world_prediction(unsigned index,const AiPathActorSnapshot& actor,
                                   std::uint32_t tick,float horizon,AiPlannedPath& out)const {
    if(index>=actors_.size())return false;
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
    return ai_traffic_world_prediction(actor,a.world_path,
        outside,a.action_owner.current.kind,tick,horizon,out);
}
bool RaceTraffic::activate(unsigned index,AiRoadCursor cursor,unsigned lane,std::uint32_t tick,
                           bool staged,float speed_limit,std::string& error,float relocation_speed,
                           float longitudinal_offset,int authored_placement) {
    auto& a=actors_[index];
    const auto& raw=lines_.find(cursor.route)->points[cursor.point];
    const auto& authored=(lane>=6?backward_lines_:lines_).find(cursor.route)->points[cursor.point];
    // 427F70 projects in the raw road frame, then sets the travel heading.
    const auto point=relocation_speed>=0?placement_point(raw,lane,longitudinal_offset):
                                         lane_point(authored,lane%6);
    a.cursor=cursor;a.lane=lane;a.road={};a.road.selected=true;a.road.road=cursor;
    a.road.category=11+int(lane%6);a.road_policy={};a.published_path={};a.driver_path={};a.maneuver={};
    a.action_owner={};a.action_records={};a.action_speed={};a.next_action_handle=1;
    a.actor_override_enabled=false;a.actor_override={};a.coordinate={};a.ended=false;
    a.authored_placement=authored_placement;a.relocation_pending=false;a.left_authored_road=false;
    const float initial_speed=float(authored.speed[11+lane%6]);
    a.speed_cap=relocation_speed>=0?speed_limit:kOrdinaryTrafficSpeedCap;
    a.target_speed=relocation_speed>=0?relocation_speed:std::min(speed_limit,initial_speed);
    const auto* forward=relocation_speed>=0?raw.forward:authored.forward;
    const float direction=relocation_speed>=0&&lane>=6?-1.F:1.F;
    a.motion={point[0],point[1],std::atan2(direction*forward[1],direction*forward[0]),a.target_speed};
    // 5992F0 draws the appearance from the world RNG after accepting the
    // placement. 59A94A -> 59B5D0 repeats the draw on relocation, even when
    // the variant is unchanged. Traffic rendering still uses the stock body.
    if(a.appearance_variants)traffic_random(world_random_?*world_random_:rng_);
    // Recycled bodies must not inherit impact compression/rest/contact caches.
    VehiclePose pose;
    if(!models_[index]->reset(a.car,pose)){error="traffic spawn model refused";return false;}
    a.driver={};a.animation={};a.impacted=false;a.crash_stop=false;
    a.pair_history={};a.active=true;a.registration=spawns+1;
    const auto heading=track_angle(direction*forward[0],direction*forward[1]);
    if(!publish(a,*impact_sessions_[index],point[2],0,heading,true,false)||!prepare_world(error)||!plan(index,tick,error)) {
        if(error.empty())error="traffic spawn controller refused";
        return false;
    }
    if(!a.active)return true;
    // The startup constructor keeps its initial speed through the first plan.
    if(!staged)a.motion.speed=a.target_speed;
    if(!publish(a,*impact_sessions_[index],point[2],0,heading)||!impact_sessions_[index]->start(a.car)){
        error="traffic spawn publication refused";return false;
    }
    a.previous_valid=false;++a.activations;++spawns;return true;
}

bool RaceTraffic::stage_authored(unsigned index,float spacing,std::int32_t clock,
                                 float* placed_progress,std::string& error) {
    if(!has_spawn_table_)return true;
    const auto& race=course_->route;
    const auto& spline=race.spline;
    const float length=spline.path_length;
    struct Candidate {const SpawnRecord* record;const Route* road;};
    std::array<std::array<Candidate,20>,2> candidates{};
    std::array<unsigned,2> count{};
    for(const auto& record:spawn_records_) {
        const auto* road=lines_.find(record.road);
        if(!road||road->points.empty())continue;
        const float raw=road->points[road->points.size()/2].path_distance;
        const float progress=race.reverse?length-raw:raw;
        // 598D46 passes zero as the distance scale to 599FC0 at startup.
        const float from=-length+20.F;
        const float to=length+120.F;
        const float relative=path_difference(progress,-length,spline);
        if(relative<=0||relative>=to-from)continue;
        for(unsigned direction=0;direction<2;++direction) {
            if(record.points[direction]>=0&&unsigned(record.points[direction])<road->points.size())
                candidates[direction][count[direction]++]={&record,road};
        }
    }
    // Keep the existing drag replay seed when there is no authored candidate.
    if(drag_&&count[0]==0&&count[1]==0)return true;
    auto& random=world_random_?*world_random_:rng_;
    for(unsigned attempt=0;attempt<100;++attempt) {
        // 598C80 rolls the traffic direction independently of race direction.
        const float unit=float(traffic_random(random)%0x7fffffffU)*4.656612873077393e-10F;
        const unsigned direction=unit<.5F?1:0;
        if(count[direction]==0)continue;
        const auto& candidate=candidates[direction][traffic_random(random)%count[direction]];
        const auto& road=*candidate.road;
        const int lane=traffic_grid_lane(road,direction==0,random);
        if(lane<0)continue;
        // 598F00 samples between the authored reference and the road midpoint.
        const unsigned reference=unsigned(candidate.record->points[direction]);
        const unsigned midpoint=unsigned(road.points.size()-1)/2;
        const unsigned left=std::min(reference,midpoint),right=std::max(reference,midpoint);
        const unsigned point=left+traffic_random(random)%std::max(1U,right-left);
        const auto& authored=(direction?backward_lines_:lines_).find(road.index)->points[point];
        const auto position=lane_point(authored,unsigned(lane)%6);
        // 59B130 tests XY distance against every active actor during startup.
        const auto too_close=[&](const VehicleState& other) {
            const float dx=position[0]-float(other.position.x);
            const float dy=position[1]-float(other.position.y);
            return std::sqrt(dx*dx+dy*dy)<spacing;
        };
        if(std::any_of(racers_.begin(),racers_.end(),
                       [&](const VehicleState* racer){return racer&&too_close(*racer);})||
           std::any_of(actors_.begin(),actors_.end(),
                       [&](const RaceTrafficActor& actor){return actor.active&&!actor.ended&&too_close(actor.car);}))
            continue;
        if(!activate(index,{road.index,std::uint16_t(point)},unsigned(lane),
                     std::uint32_t(clock),true,candidate.record->speed_limit,error,-1.F,0.F,
                     int(candidate.record-spawn_records_.data())))return false;
        if(!actors_[index].active)continue;
        if(placed_progress) {
            const float raw=road.points[point].path_distance;
            *placed_progress=race.reverse?length-raw:raw;
        }
        break;
    }
    return true;
}

bool RaceTraffic::stage_grid(const std::array<const VehicleState*,4>& racers,
                             const std::array<const RaceContactBox*,4>& shapes,
                             float spacing,float clearance,std::int32_t clock,std::string& error) {
    if(!course_||grid_staged_||!racers[0]||!std::isfinite(spacing)||spacing<=0||
       !std::isfinite(clearance)||clearance<0||(drag_&&course_->route.start<0)) {
        error="invalid traffic grid";return false;
    }
    racers_=racers;racer_shapes_=shapes;world_clock_=clock;
    if(!prepare_world(error))return false;
    if(!(drag_?stage_drag_grid(spacing,clearance,clock,error):
               stage_ordinary_grid(spacing,clearance,clock,error)))return false;
    // The slot stays registered if initial placement failed. The
    // controller must retry it through the pending branch.
    for(auto& actor:actors_)if(!actor.active)actor.relocation_pending=true;
    grid_staged_=true;
    return true;
}

bool RaceTraffic::stage_drag_grid(float spacing,float clearance,std::int32_t clock,std::string& error) {
    const auto& racers=racers_;
    const auto& race=course_->route;
    const auto& spline=race.spline;
    const auto progress=[&](float raw){return race.reverse?spline.path_length-raw:raw;};
    const float start=race.boundaries.at(unsigned(race.start)).percent*.01F*spline.path_length;
    auto& random=world_random_?*world_random_:rng_;
    const auto unit=[&](){return float(traffic_random(random)%0x7fffffffU)*4.656612873077393e-10F;};
    std::array<float,kRaceTrafficCapacity> placed{};
    for(unsigned index=0;index<actors_.size();++index) {
        // 5991A0 tries one authored spacing interval per retry, starting at
        // traffic config ID minus 50 (the index in this pool).
        for(unsigned attempt=0;attempt<100;++attempt) {
            const bool forward=(unit()<.5F)!=race.reverse;
            const auto candidate=ai_traffic_drag_grid_point(lines_,spline,race.reverse,
                float(index+attempt)*spacing,forward);
            if(candidate.route<0)continue;
            const auto& route=*lines_.find(candidate.route);
            int lane=traffic_grid_lane(route,forward,random);
            if(lane<0)lane=traffic_grid_lane(route,!forward,random);
            if(lane<0)continue;
            const float position=progress(route.points[candidate.point].path_distance);
            if(std::abs(path_difference(position,start,spline))<clearance)continue;
            bool blocked=false;
            for(unsigned i=0;i<index;++i)if(actors_[i].active&&
                ai_traffic_spawn_gap_blocked(path_difference(position,placed[i],spline),spacing,true))blocked=true;
            for(unsigned i=0;i<racers.size();++i)if(racers[i]&&
                ai_traffic_spawn_gap_blocked(path_difference(position,progress(world_nodes_[i].progress),spline),spacing,false))blocked=true;
            if(blocked)continue;
            if(!activate(index,candidate,unsigned(lane),std::uint32_t(clock),true,
                         std::numeric_limits<float>::infinity(),error))return false;
            if(!actors_[index].active)continue;
            placed[index]=position;
            break;
        }
        if(!actors_[index].active&&
           !stage_authored(index,spacing,clock,&placed[index],error))return false;
    }
    return true;
}

bool RaceTraffic::stage_ordinary_grid(float spacing,float clearance,std::int32_t clock,std::string& error) {
    const auto& race=course_->route;
    const auto& spline=race.spline;
    const auto progress=[&](float raw){return race.reverse?spline.path_length-raw:raw;};
    const float start=race.start<0?progress(world_nodes_[0].progress):
        race.boundaries.at(unsigned(race.start)).percent*.01F*spline.path_length;
    auto& random=world_random_?*world_random_:rng_;
    std::array<std::array<const Route*,100>,2> roads{};
    std::array<unsigned,2> road_counts{};
    unsigned forward_weight=0;
    // 59AF20 builds the draw bound from forward lanes even for reverse draws.
    for(int id=0;id<100;++id) {
        const auto* road=lines_.find(std::int16_t(id));
        if(!road)continue;
        for(unsigned direction=0;direction<2;++direction) {
            const auto first=road->lane_enabled.begin()+6*direction;
            if(std::none_of(first,first+6,[](bool enabled){return enabled;}))continue;
            roads[direction][road_counts[direction]++]=road;
            if(direction==0)forward_weight+=unsigned(road->points.size());
        }
    }
    for(unsigned index=0;index<actors_.size();++index) {
        if(!stage_authored(index,spacing,clock,nullptr,error))return false;
        if(actors_[index].active||!forward_weight)continue;
        const Route* selected=nullptr;
        unsigned point=0;
        for(unsigned attempt=0;attempt<100;++attempt) {
            const float unit=float(traffic_random(random)%0x7fffffffU)*4.656612873077393e-10F;
            const bool forward=(unit<.5F)!=race.reverse;
            const unsigned direction=forward?0:1;
            const unsigned roll=traffic_random(random)%forward_weight;
            unsigned cumulative=0;
            // 59B2C0 leaves the last candidate intact if the draw exceeds
            // the selected direction's weight.
            for(unsigned i=0;i<road_counts[direction];++i) {
                const auto* road=roads[direction][i];
                cumulative+=unsigned(road->points.size());
                if(roll<cumulative) {
                    selected=road;
                    point=cumulative-roll;
                    break;
                }
            }
            if(!selected||point+1>=selected->points.size())continue;
            const auto& sample=selected->points[point];
            if(!ai_traffic_spawn_point_allowed(sample.unknown_84,true)||
               !ai_traffic_spawn_point_allowed(sample.unknown_84,false))continue;
            const float at=progress(sample.path_distance);
            if(std::abs(path_difference(at,start,spline))<clearance)continue;
            bool blocked=false;
            for(unsigned i=0;i<world_nodes_.size();++i) {
                if(!world_nodes_[i].enabled||
                   (i>=4&&(!actors_[i-4].active||actors_[i-4].ended)))continue;
                const float delta=path_difference(at,progress(world_nodes_[i].progress),spline);
                if(ai_traffic_spawn_gap_blocked(delta,spacing,i>=4)) {blocked=true;break;}
            }
            if(blocked)continue;
            int lane=traffic_grid_lane(*selected,forward,random);
            if(lane<0)lane=traffic_grid_lane(*selected,!forward,random);
            if(lane<0)continue;
            if(!activate(index,{selected->index,std::uint16_t(point)},unsigned(lane),std::uint32_t(clock),
                         true,std::numeric_limits<float>::infinity(),error))return false;
            if(actors_[index].active)break;
        }
    }
    return true;
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
        return path_difference(left,right,course_->route.spline);
    };
    bool stopped=false;
    for(const auto& other:actors_)if(other.active&&other.car.owner_speeds.magnitude<1&&
       std::abs(difference(other.progress,raw_progress))<250)stopped=true;
    float spacing;
    if(!ai_traffic_spawn_spacing(zone,density_,stopped,random,spacing))return false;
    const auto sample=traffic_random(random)%0x7fffffffU;
    const unsigned start=sample%lines_.routes.size();
    for(unsigned ri=0;ri<lines_.routes.size();++ri) {
        const auto& route=lines_.routes[(start+ri)%lines_.routes.size()];
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
                if(!activate(index,cursor,lane,std::uint32_t(world_clock_>=0?world_clock_:tick*4000/60),
                             false,24.58650016784668F,error))
                    return false;
                if(a.active)return true;
            }
        }
    }
    return false;
}
bool RaceTraffic::relocate_ordinary(unsigned index,std::uint32_t clock,float spacing,bool wrong_way,
                                    const AiRoadCursor* human_road,std::string& error) {
    auto& random=world_random_?*world_random_:rng_;
    // 59B370 draws a reference from the human list even with one entry.
    traffic_random(random);
    const auto& player=*racers_[0];
    const auto& race=course_->route;
    const bool reverse=race.reverse;
    const float length=race.spline.path_length;
    const float reference=racer_coordinates_[0].distance;
    const auto ahead=[&](float raw) {
        return path_difference(reverse?length-raw:raw,reference,race.spline);
    };
    const auto place=[&](AiRoadCursor cursor,unsigned lane,float limit,float speed,float offset,int authored=-1) {
        return retire(index,error)&&activate(index,cursor,lane,clock,true,limit,error,speed,offset,authored);
    };

    // 599830 tries the authored record window first. 599C80 passes the
    // selected human's progress to 598B60. The only human in this traffic
    // world is that same driver, so its distance in the 50 m gate is zero.
    const float signed_speed=player.owner_speeds.forward;
    if(traffic_random_unit(random)<(signed_speed<20.F?.35F:1.F)&&!spawn_records_.empty()) {
        const bool backward=traffic_random_unit(random)<.5F;
        const bool scaled=signed_speed>=20.F;
        const float left=scaled?reference:reference-60.F,right=scaled?reference:reference+60.F;
        std::array<const SpawnRecord*,20> candidates{};
        unsigned count=0;
        for(const auto& record:spawn_records_) {
            const auto* road=lines_.find(record.road);
            if(!road||road->points.empty()||record.points[backward]<0)continue;
            const float raw=road->points[road->points.size()/2].path_distance;
            const float near=scaled?std::max(20.F,signed_speed*record.near_scale):0.F;
            const float far=scaled?std::max(120.F,signed_speed*record.far_scale):0.F;
            // 5AB160 and 41F5F0 both use distance in the race direction.
            // 599FC0 adjusts the window span after sampling the left edge.
            const float delta=path_difference(reverse?length-raw:raw,left,race.spline);
            if(delta>0.F&&delta<(right-left)+far-near&&count<candidates.size())
                candidates[count++]=&record;
        }
        if(count) {
            const auto& record=*candidates[traffic_random(random)%count];
            const auto& road=*lines_.find(record.road);
            const unsigned point=scaled?unsigned(record.points[backward]):
                backward?unsigned(road.points.size()-1):0;
            const int lane=traffic_grid_lane(road,!backward,random);
            if(lane>=0&&point<road.points.size()) {
                const float gap=traffic_random_unit(random)*(record.gap_max-record.gap_min)+record.gap_min;
                const auto& authored=road.points[point];
                const float offset=scaled?record.longitudinal_offsets[backward]:0.F;
                const auto at=placement_point(authored,unsigned(lane),offset);
                bool blocked=false;
                for(unsigned i=0;i<actors_.size();++i) {
                    const auto& other=actors_[i];
                    if(i==index||!other.active||other.cursor.route!=road.index||other.lane/6!=unsigned(lane)/6)continue;
                    const auto& p=other.car.position;
                    const auto dx=p.x-at[0],dy=p.y-at[1];
                    if(dx*dx+dy*dy<gap*gap)blocked=true;
                }
                if(!blocked) {
                    // 599C80 clamps authored initial speed to the record cap.
                    const float speed=std::min(record.speed_limit,float(authored.speed[11+lane]));
                    return place({road.index,std::uint16_t(point)},unsigned(lane),record.speed_limit,speed,offset,
                                 int(&record-spawn_records_.data()));
                }
            }
        }
    }

    const bool lane_forward=(traffic_random_unit(random)<.5F)!=reverse;
    AiRoadSelectionInput selection;
    selection.position={float(player.position.x),float(player.position.y),float(player.position.z)};
    selection.progress=reverse?length-reference:reference;selection.maximum_distance=999999.F;
    selection.direction=reverse;selection.role=1;selection.allow_outside_width=true;
    AiRoadCursor cursor;bool found=false;
    // 428150/428160 read the current human controller's retained raw road.
    if(human_road) {
        const auto* road=lines_.find(human_road->route);
        found=road&&human_road->route<=119&&human_road->point<road->points.size();
        if(found)cursor=*human_road;
    } else if(!ai_road_select(lines_,selection,found,cursor)) {
        error="traffic relocation road refused";
        return false;
    }
    if(!found)return retire(index,error);
    // 59A330 refuses the raw cursor before 59A45D can consume the
    // low-speed direction draw.
    const bool slow=player.owner_speeds.magnitude<kOrdinaryTrafficSpeedCap;
    const bool change_direction=slow?traffic_random_unit(random)<.5F:wrong_way;
    const bool forward=reverse==change_direction;

    // 4563D0 follows the road links, including connector projections. The
    // first search is 250 m from the player's current road point. Rejected
    // candidates proceed from there at max(spacing, chord) intervals.
    const float old_gap=ahead(actors_[index].progress);
    const auto walk=[&](AiRoadCursor from,float distance,AiRoadCursor& next) {
        return ai_traffic_walk_road(lines_,events_,from,distance,forward,next)&&
               (next.route!=from.route||next.point!=from.point);
    };
    if(!walk(cursor,250.F,cursor))return retire(index,error);
    for(unsigned attempt=0;attempt<0x19000;++attempt) {
        const auto& road=*lines_.find(cursor.route);
        const auto& point=road.points[cursor.point];
        const float delta=ahead(point.path_distance);
        if(delta>500.F)return retire(index,error);
        int lane=traffic_grid_lane(road,lane_forward,random);
        if(lane<0)lane=traffic_grid_lane(road,!lane_forward,random);
        bool blocked=std::fabs(delta)<250.F;
        // 59B1E0 checks active placement participants (+4D0). Only
        // traffic occupies that list for this live call; 59AD90 protects
        // the human position with its separate 250 m exclusion.
        for(unsigned j=0;j<actors_.size();++j) {
            if(j==index||!actors_[j].active)continue;
            if(ai_traffic_spawn_gap_blocked(
                path_difference(ahead(actors_[j].progress),delta,race.spline),spacing,true))blocked=true;
        }
        if(lane>=0&&!blocked&&ai_traffic_spawn_point_allowed(point.unknown_84,lane_forward)) {
            if(!actors_[index].relocation_pending&&((forward!=reverse&&old_gap>0&&delta>old_gap)||
                (forward==reverse&&old_gap<0&&delta<old_gap)))return retire(index,error);
            // 59A6C6 sets the cap independently of the fallback's initial speed.
            return place(cursor,unsigned(lane),kOrdinaryTrafficSpeedCap,float(point.speed[11+lane]),0.F);
        }
        const auto last=cursor;
        const float chord=point.distance_to_next;
        float distance=std::max(spacing,chord);
        // 59A739 retries while the traversal still reports the same raw
        // point. A failed traversal leaves that point unchanged.
        for(unsigned retry=0;;++retry) {
            if(retry==32||!std::isfinite(distance))return retire(index,error);
            AiRoadCursor next;
            if(walk(last,distance,next)) {
                cursor=next;
                break;
            }
            distance+=distance;
        }
    }
    return retire(index,error);
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
bool RaceTraffic::recycle_before_controllers(const std::array<const VehicleState*,4>& racers,
    const std::array<const RaceContactBox*,4>& shapes,const std::array<bool,4>& ghosts,
    std::uint64_t tick,std::int32_t clock,float dt,bool grid_held,bool wrong_way,
    const AiRoadCursor* human_road,std::string& error) {
    // The game's world updates 598600 before rival controllers. They see a
    // newly placed body in the same epoch, and share the same RNG stream.
    if(relocation_tick_==tick)return true;
    relocation_tick_=tick;
    //420B80/57A530 copy Driver+10 before420C85 runs the traffic manager.
    //Relocation may clear the driver or reuse its actor, but a query in this
    //epoch must retain the publication from before that manager pass.
    for(auto& actor:actors_)actor.world_path=actor.driver_path;
    // 598600 maintains intersections even while 59B710 has parked
    // the traffic actors for the start animation.
    for(auto& table:intersections_)if(!ai_traffic_intersection_step(table)){
        error="traffic intersection phase refused";return false;
    }
    if(drag_||!grid_staged_||grid_held||(recycle_seconds_-=dt)>0.F)return true;
    recycle_seconds_=1.F;
    racers_=racers;racer_shapes_=shapes;racer_ghosts_=ghosts;
    if(!prepare_world(error))return false;
    const auto& spline=course_->route.spline;
    const float player=racer_coordinates_[0].distance;
    const auto oriented=[&](float raw){return course_->route.reverse?spline.path_length-raw:raw;};
    // 59B8D0 counts stopped traffic only. A stopped player or rival does
    // not replace the zone spacing with the no-traffic sentinel.
    bool stopped=false;
    for(const auto& actor:actors_) {
        if(!actor.relocation_pending&&actor.active&&actor.car.owner_speeds.magnitude<1.F&&
           std::fabs(path_difference(oriented(actor.progress),player,spline))<250.F) {
            stopped=true;
            break;
        }
    }
    const float point[]={float(racers_[0]->position.x),float(racers_[0]->position.y)};
    const int zone_index=track_path_zone_index(course_->track,4,course_->route.reverse?1:0,point);
    const auto* zone=zone_index>=0?&course_->track.zones[unsigned(zone_index)]:nullptr;
    auto& random=world_random_?*world_random_:rng_;
    float spacing;
    if(!ai_traffic_spawn_spacing(zone,density_,stopped,random,spacing))return false;
    for(unsigned probes=0;probes<actors_.size();++probes) {
        recycle_=(recycle_+1)%actors_.size();
        const auto& actor=actors_[recycle_];
        if(!actor.relocation_pending) {
            const float gap=path_difference(oriented(actor.progress),player,spline);
            const auto& pace=racers_[0]->owner_speeds;
            const bool near_human=std::fabs(gap)<250.F;
            if(actor.authored_placement>=0) {
                // Policy 5: a car past its authored exit can change roads
                // unless a slow human is still within 50 m.
                const bool near_slow_human=pace.forward<20.F&&std::fabs(gap)<50.F;
                if(near_human&&(!actor.left_authored_road||near_slow_human))continue;
            } else {
                // Policy 4: only the direction/pace branch can admit a
                // nonpending body. At equal pace neither branch qualifies.
                const float speed=actor.car.owner_speeds.magnitude;
                const bool same_direction=(actor.lane>=6)==course_->route.reverse;
                const bool eligible=gap>0.F?same_direction&&speed>pace.magnitude:
                                             !same_direction||speed<pace.magnitude;
                if(near_human||!eligible)continue;
            }
        }
        if(!relocate_ordinary(recycle_,std::uint32_t(clock>=0?clock:tick*4000/60),spacing,wrong_way,human_road,error))return false;
        break;
    }
    return true;
}
bool RaceTraffic::prepare_frame(const std::array<const VehicleState*,4>& racers,std::uint64_t tick,std::string& error,std::int32_t world_clock,float dt,const std::array<bool,4>& ghosts,const std::array<const RaceContactBox*,4>& racer_shapes,bool grid_held) {
    if(!std::isfinite(dt)||dt<=0||dt>.1F){error="invalid traffic frame interval";return false;}
    if(frame_prepared_){error="traffic frame already prepared";return false;}
    frame_seconds_=dt;world_clock_=world_clock;
    last_component_interval=-1;
    if(!course_||!racers[0]){error="traffic owner missing";return false;}
    error.clear();racers_=racers;racer_shapes_=racer_shapes;racer_ghosts_=ghosts;
    for(auto& a:actors_){a.previous_pose=race_contact_pose(a.car);a.previous_valid=a.active;}
    if(!recycle_before_controllers(racers,racer_shapes,ghosts,tick,world_clock,dt,grid_held,false,nullptr,error))return false;
    if(tick%60==0&&(!grid_staged_||(drag_&&!grid_held))) {
        auto& a=actors_[recycle_];bool near=false;
        for(const auto* r:racers)if(r&&distance2(a.car.position,r->position)<250.F*250.F)near=true;
        if(a.active&&!near&&!retire(recycle_,error))return false;
        if(!a.active&&!spawn(recycle_,racers,tick,error)&&!error.empty())return false;
        recycle_=(recycle_+1)%actors_.size();
    }
    if(!prepare_world(error))return false;
    auto& registered=prepared_actors_;prepared_count_=0;auto& count=prepared_count_;
    for(unsigned i=0;i<actors_.size();++i)if(actors_[i].active)registered[count++]=i;
    const bool fixed_group=grid_staged_&&!drag_;
    if(!fixed_group)
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
        // 583DA0 also stops a blocked traffic car with a live contact and
        // a nonzero driver target, even if no individual pair crossed 30.
        if(a.car.collision_body_flag>0&&a.car.owner_speeds.magnitude<1.F&&a.target_speed>1.F)
            a.crash_stop=true;
        // The original scheduler indexes the complete traffic registry.
        // A hidden body keeps its slot when it goes out of simulation.
        const unsigned group_slot=fixed_group?i:slot;
        const unsigned group_size=fixed_group?unsigned(actors_.size()):count;
        bool full=false;
        // 41F80C clears the world frame when a race is constructed.
        // The 100000 at 41FB64 belongs to teardown.
        const auto world_frame=std::uint32_t(tick);
        if(!ai_traffic_full_update(world_frame,group_slot,group_size,full)){error="traffic scheduler refused";return false;}
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
    TrafficMotionInput motion;motion.front_axle=h.wheels[0].longitudinal;motion.rear_axle=h.wheels[2].longitudinal;
    motion.throttle=a.driver.pedals.throttle;motion.brake=a.driver.pedals.brake;
    motion.dt=dt;motion.steering=a.driver.steering.word;
    if(!traffic_motion_step(motion,a.motion)){error="traffic mover refused";return false;}
    a.motion.heading=std::remainder(a.motion.heading,float(tau));
    // 575703..575727 retains the last height and heading until the ground fit.
    if(!publish(a,*impact_sessions_[i],float(a.car.position.z),dt,a.published_heading)) {
        error="traffic publication refused";return false;
    }
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
