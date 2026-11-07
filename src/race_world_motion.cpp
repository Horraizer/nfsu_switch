#include "nfsu/race_session.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace nfsu {
namespace {
std::array<float,16> multiply(const std::array<float,16>& a,const std::array<float,16>& b){
    std::array<float,16> out{};
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)
        out[row*4+col]=((a[row*4]*b[col]+a[row*4+1]*b[4+col])+a[row*4+2]*b[8+col])+a[row*4+3]*b[12+col];
    return out;
}
}
void RaceSession::initialize_world_object(unsigned index){
    const auto& source=course_.world_objects[index];auto& object=world_objects_[index];
    object.enabled=true;object.box=race_contact_box(source.hull);
    object.spec.handling.mass=1;object.spec.handling.inertia={1,1,1};
    object.spec.handling.centre_of_mass={};
    object.bounds=race_track_body_bounds(object.spec,object.box);
    object.placement=source.transform;
    auto& state=object.state;state.orientation=state.rigid_orientation={1,0,0,0};
    state.position={source.transform[12],source.transform[13],source.transform[14]};
    state.rigid_body.position={source.transform[12],source.transform[13],source.transform[14]};
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)state.rigid_body.axes[r][c]=source.transform[4*r+c];
    state.rigid_body.snapshot_axes=state.rigid_body.axes;
    state.rigid_body_ready=true;state.body_gravity=false;
    object.history=race_contact_pose(state);
}
bool RaceSession::prepare_world_motion(const std::filesystem::path& root,std::string& error){
    std::vector<std::uint32_t> wanted;
    for(const auto& source:course_.world_objects)if(source.motion_collision)
        for(unsigned j=0;j<2;++j)if(std::find(wanted.begin(),wanted.end(),source.animation[j])==wanted.end())wanted.push_back(source.animation[j]);
    std::vector<WorldMotionClip> clips;
    if(!wanted.empty()){
        std::ifstream input(root/"Global/INGAMEB.BUN",std::ios::binary|std::ios::ate);
        if(!input){error="world motion bank unavailable";return false;}
        const auto size=input.tellg();
        if(size<=0||size>32*1024*1024){error="world motion bank size";return false;}
        eagl::Bytes bytes(static_cast<std::size_t>(size));input.seekg(0);
        if(!input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))){error="world motion bank read failed";return false;}
        if(!decode_world_motion_clips(bytes,wanted,clips,error))return false;
    }
    world_motion_clips_=std::move(clips);
    for(unsigned i=0;i<course_.world_objects.size();++i)if(course_.world_objects[i].motion_collision){
        initialize_world_object(i);auto& object=world_objects_[i];object.animated=true;
        for(unsigned j=0;j<2;++j){
            const auto hash=course_.world_objects[i].animation[j];
            const auto at=std::find_if(world_motion_clips_.begin(),world_motion_clips_.end(),[&](const auto& c){return c.hash==hash;});
            if(at==world_motion_clips_.end()){error="world motion clip binding absent";return false;}
            object.clips[j]=std::size_t(at-world_motion_clips_.begin());
        }
    }
    return reset_world_motion(error);
}
bool RaceSession::reset_world_motion(std::string& error){
    unsigned cars=0;
    for(unsigned i=0;i<world_objects_.size();++i){
        auto& object=world_objects_[i];if(!object.animated)continue;
        const auto& source=course_.world_objects[i];object.reference_ready=false;object.train_prediction={};
        object.clock={source.animation_flags,source.animation_rate==0?1.F:source.animation_rate,
            float(std::max(world_motion_clips_[object.clips[0]].frames.size(),world_motion_clips_[object.clips[1]].frames.size())),0,source.animation_hold,0};
        if(!(object.clock.flags&0xf0))object.clock.flags|=0x10;
        if(source.type==0x427a6453U)object.clock.phase=float(cars++)*6.F;
    }
    for(unsigned i=0;i<world_objects_.size();++i){
        auto& object=world_objects_[i];if(!object.animated)continue;
        if(course_.world_objects[i].type==0x24cebc53U)object.clock.phase=float(cars)*6.F;
        if(!place_world_motion(i,true,error))return false;
        object.state.simulation_seconds=car().simulation_seconds;
        object.history=race_contact_pose(object.state);
    }
    return true;
}
bool RaceSession::place_world_motion(unsigned index,bool reset,std::string& error){
    auto& object=world_objects_[index];
    eagl::Pose translation,rotation;
    if(!sample_world_motion_clip(world_motion_clips_[object.clips[0]],object.clock.phase,translation)||
       !sample_world_motion_clip(world_motion_clips_[object.clips[1]],object.clock.phase,rotation)){
        error="world motion pose sample refused";return false;
    }
    for(unsigned j=8;j<11;++j)rotation[j]=translation[j];
    const auto pose=multiply(world_motion_pose_matrix(rotation),course_.world_objects[index].transform);
    auto& state=object.state;
    if(!reset&&frame_seconds_>double(.0001F)){
        const float scale=1.F/float(frame_seconds_);
        state.velocity={double((pose[12]-object.placement[12])*scale),double((pose[13]-object.placement[13])*scale),double((pose[14]-object.placement[14])*scale)};
    } else if(reset)state.velocity={};
    object.placement=pose;
    state.position={pose[12],pose[13],pose[14]};state.rigid_body.position={pose[12],pose[13],pose[14]};
    state.rigid_body.momentum={float(state.velocity.x),float(state.velocity.y),float(state.velocity.z)};
    //579C00 publishes translation delta/dt, but does not synthesize angular velocity.
    state.angular_velocity={};state.rigid_body.angular_momentum={};
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)state.rigid_body.axes[r][c]=pose[4*r+c];
    state.rigid_body.snapshot_axes=state.rigid_body.axes;return true;
}
bool RaceSession::bind_world_obstacle(std::uint32_t type,int enter,int leave,WorldObstacleBinding& binding)const {
    if(binding.bound())return binding.object<world_objects_.size()&&world_objects_[binding.object].animated;
    for(unsigned i=0;i<world_objects_.size();++i) {
        const auto& object=world_objects_[i];
        if(!object.animated||course_.world_objects[i].type!=type)continue;
        eagl::Pose t,q;
        if(!sample_world_motion_clip(world_motion_clips_[object.clips[0]],float(enter),t)||
           !sample_world_motion_clip(world_motion_clips_[object.clips[1]],float(enter),q))return false;
        for(unsigned k=8;k<11;++k)q[k]=t[k];
        const auto pose=multiply(world_motion_pose_matrix(q),course_.world_objects[i].transform);
        const float point[2]={pose[12],pose[13]};TrackPathCoordinate coordinate;
        update_track_path_coordinate(course_.route.spline,course_.track,coordinate,point);
        binding={i,coordinate.distance,float(enter),float(leave)};return true;
    }
    return true; // Original class registry lookup may legitimately find no mover.
}
bool RaceSession::world_obstacle_status(const WorldObstacleBinding& binding,const VehicleState& actor,
    float progress,WorldObstacleStatus& status)const {
    status={};if(!binding.bound())return true;
    if(binding.object>=world_objects_.size()||!world_objects_[binding.object].animated)return false;
    const auto& head=world_objects_[binding.object];const WorldObjectBody* tail=&head;
    if(course_.world_objects[binding.object].type==0x24cebc53U)
        for(unsigned i=0;i<world_objects_.size();++i)if(world_objects_[i].animated&&course_.world_objects[i].type==0x427a6453U){tail=&world_objects_[i];break;}
    WorldObstacleWindow window;window.bound=window.head_present=window.tail_present=true;
    window.head=head.clock;window.tail=tail->clock;window.enter=binding.enter;window.leave=binding.leave;window.delay=head.train_prediction.delay;
    const auto& motion=actor.owner_motion;
    const float acceleration=(motion.acceleration[0]*motion.axes[0][0]+motion.acceleration[1]*motion.axes[0][1])+motion.acceleration[2]*motion.axes[0][2];
    const float distance=world_obstacle_distance(binding.distance,progress,course_.route.spline.path_length,!course_.route.spline.open);
    status.available=true;status.passed=distance<0;
    status.clear=world_obstacle_clear(window,distance,actor.owner_speeds.magnitude,acceleration);return true;
}
bool RaceSession::advance_world_motion(float dt,int lane,std::string& error){
    for(unsigned i=0;i<world_objects_.size();++i){
        auto& object=world_objects_[i];if(!object.animated)continue;
        object.history=race_contact_pose(object.state);
        advance_world_motion_clock(object.clock,dt);
        if(!place_world_motion(i,false,error))return false;
        object.state.simulation_seconds=car().simulation_seconds;
    }
    //58E2E0 admits authored event10006 by strict sphere and direction mask.
    bool trigger=false;
    for(const auto& sphere:course_.train_triggers){
        //4281E0 queries a point predicted0.15seconds ahead of published motion.
        const auto& velocity=car().owner_motion.velocity;
        const float x=(float(car().position.x)+velocity[0]*.15F)-sphere.centre[0];
        const float y=(float(car().position.y)+velocity[1]*.15F)-sphere.centre[1];
        const float z=(float(car().position.z)+velocity[2]*.15F)-sphere.centre[2];
        if((sphere.player_mask&(1U<<unsigned(course_.route.spline.reverse)))&&
           (x*x+y*y)+z*z<sphere.radius*sphere.radius){trigger=true;break;}
    }
    bool release=false;
    if(trigger)for(unsigned i=0;i<world_objects_.size();++i){
        auto& engine=world_objects_[i];if(!engine.animated||course_.world_objects[i].type!=0x24cebc53U)continue;
        if(!engine.reference_ready){
            const auto saved=engine.clock.phase;engine.clock.phase=52.F;
            const auto old_state=engine.state;const auto old_placement=engine.placement;
            if(!place_world_motion(i,true,error))return false;
            const float point[2]={float(engine.state.position.x),float(engine.state.position.y)};
            update_track_path_coordinate(course_.route.spline,course_.track,engine.reference,point);
            engine.state=old_state;engine.placement=old_placement;engine.clock.phase=saved;
            engine.reference_ready=true;
        }
        float distance=engine.reference.distance-progress().coordinate.distance;
        const auto& spline=course_.route.spline;
        if(!spline.open){
            if(distance<spline.path_length*-.5F)distance+=spline.path_length;
            else if(distance>spline.path_length*.5F)distance-=spline.path_length;
        }
        const auto& owner=car().owner_motion;
        const float acceleration=(owner.acceleration[0]*owner.axes[0][0]+owner.acceleration[1]*owner.axes[0][1])+owner.acceleration[2]*owner.axes[0][2];
        const float velocity=car().owner_speeds.magnitude;
        const float arrival=world_train_arrival(distance,velocity,acceleration);
        const float lead=world_train_lead(lane);
        if(arrival>=0)engine.train_prediction={true,arrival-lead,engine.reference.distance-velocity*lead};
        if(arrival>=0&&(arrival<lead||distance<50.F))release=true;
    }
    if(release)for(unsigned i=0;i<world_objects_.size();++i)
        if(course_.world_objects[i].type==0x24cebc53U||course_.world_objects[i].type==0x427a6453U)world_objects_[i].clock.flags&=~0x80U;
    return true;
}
} // namespace nfsu
