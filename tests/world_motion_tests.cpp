#include "nfsu/world_motion.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/draw_execution.h"
#include "nfsu/arg_path.h"
#include "nfsu/posix_file_system.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
unsigned failures=0;
void check(bool okay,const char* message){if(!okay){++failures;std::cerr<<"FAIL "<<message<<'\n';}}
void clocks(){
    struct Case {unsigned flags;float rate,phase,dt,after,after_rate,hold;unsigned status;};
    const Case cases[]={
#include "world_motion_clock_native.inc"
    };
    unsigned index=0;
    for(const auto& c:cases){
        nfsu::WorldMotionClock state{c.flags,c.rate,99.F,c.phase,.2F,.1F};
        const auto status=nfsu::advance_world_motion_clock(state,c.dt);
        if(status!=c.status||state.phase!=c.after||state.rate!=c.after_rate||state.hold_elapsed!=c.hold){
            std::cerr<<"FAIL native clock "<<index<<" phase="<<state.phase<<" expected="<<c.after<<'\n';++failures;
        }
        ++index;
    }
}
void moving_contacts(){
    using namespace nfsu;
    struct Case {float speed,side;bool world_first;ContactVector velocity,momentum,angular;unsigned state,gate;float magnitude;unsigned seed;};
    const Case cases[]={
#include "world_motion_contact_native.inc"
    };
    auto spec=stock_240sx_vehicle_spec();spec.handling.mass=2;spec.handling.inertia={1,1,1};
    spec.handling.centre_of_mass={};spec.handling.alternate_yaw_scale=1;
    spec.handling.wheels[0].lateral=-.7F;spec.handling.wheels[1].lateral=.7F;
    VehicleHull hull;
    for(unsigned i=0;i<8;++i)hull.points.push_back({i&1?-1.F:1.F,i&2?-1.F:1.F,i&4?-1.F:1.F});
    const auto box=race_contact_box(hull);unsigned index=0;
    for(const auto& c:cases){
        VehicleState car,object;car.orientation=object.orientation={1,0,0,0};car.velocity={c.speed,0,0};
        object.position={1.9F,c.side,.2F};object.velocity={c.velocity[0],c.velocity[1],c.velocity[2]};
        car.published_support=4;car.alternate_yaw_active=object.alternate_yaw_active=false;
        vehicle_import_rigid(spec.handling,car);vehicle_import_rigid(spec.handling,object);
        ContactRecord record;unsigned seed=12345;RaceContactContext context{record,seed,5};
        RaceContactJob job;job.admitted=true;job.query.witness_a=job.query.witness_b={.95F,c.side*.5F,.1F};
        job.actor_up[0]=job.actor_up[1]={0,0,1};RacePairContact result;
        auto& a=c.world_first?object:car;auto& b=c.world_first?car:object;
        const bool okay=resolve_swept_race_pair(a,spec,box,b,spec,box,result,race_contact_pose(a),race_contact_pose(b),
            &context,&job,{c.world_first,!c.world_first});
        double error=std::abs(double(record.magnitude_sum)-c.magnitude);
        for(unsigned k=0;k<3;++k){error+=std::abs(double(car.rigid_body.momentum[k])-c.momentum[k]);
            error+=std::abs(double(car.rigid_body.angular_momentum[k])-c.angular[k]);}
        if(!okay||!std::isfinite(error)||error>.00002||car.collision_state!=c.state||
           unsigned(record.state_gate)!=c.gate||seed!=c.seed){
            std::cerr<<"FAIL native moving contact "<<index<<" error="<<error<<'\n';++failures;
        }
        check(object.velocity.x==c.velocity[0]&&object.velocity.y==c.velocity[1]&&object.velocity.z==c.velocity[2],
              "animated kind4 velocity remains immutable under impact");
        ++index;
    }
}
void arrivals(){
    struct Case {float distance,speed,acceleration,arrival;};
    const Case cases[]={
#include "world_motion_arrival_native.inc"
    };
    for(const auto& c:cases){
        const auto actual=nfsu::world_train_arrival(c.distance,c.speed,c.acceleration);
        check(std::isfinite(actual)&&std::abs(actual-c.arrival)<=std::max(.00001F,std::abs(c.arrival)*.000001F),"original train arrival time");
    }
    check(nfsu::world_train_lead(10)==-1&&nfsu::world_train_lead(17)==-1,"invalid lane lead");
    for(int lane=11;lane<=16;++lane)
        check(std::abs(nfsu::world_train_lead(lane)-(1.2F+.15F*float(lane-11)))<.000001F,"original lane-context train lead");
}
void clips(const std::filesystem::path& root){
    struct Case {std::uint32_t hash;float frame;nfsu::eagl::Pose pose;std::array<float,16> matrix;};
    const Case cases[]={
#include "world_motion_samples_native.inc"
    };
    std::vector<nfsu::WorldMotionClip> clips;std::string error;
    const bool loaded=nfsu::load_world_motion_clips(nfsu::posix_file_system(),root,
        {0x9d2624dd,0x9d2624da,0xa81847b9,0xa81847b6},clips,error);
    check(loaded,error.c_str());if(!loaded)return;
    float worst=0;
    for(const auto& c:cases){
        const auto clip=std::find_if(clips.begin(),clips.end(),[&](const auto& item){return item.hash==c.hash;});
        check(clip!=clips.end(),"retail motion clip found");if(clip==clips.end())continue;
        check(clip->frames.size()==((c.hash==0x9d2624dd||c.hash==0x9d2624da)?100U:690U),"authored clip length");
        nfsu::eagl::Pose pose;
        check(nfsu::sample_world_motion_clip(*clip,c.frame,pose),"sample motion clip");
        float difference=0;
        for(unsigned i=0;i<12;++i)difference=std::max(difference,std::abs(pose[i]-c.pose[i]));
        const auto matrix=nfsu::world_motion_pose_matrix(pose);
        for(unsigned i=0;i<16;++i)difference=std::max(difference,std::abs(matrix[i]-c.matrix[i]));
        worst=std::max(worst,difference);
        if(!std::isfinite(difference)||difference>0.0001F){
            std::cerr<<"FAIL native clip "<<std::hex<<c.hash<<std::dec<<" frame="<<c.frame<<" error="<<difference<<'\n';++failures;
        }
    }
    std::cout<<"native clip max error="<<worst<<'\n';
}
}
namespace nfsu {
struct RaceWorldContactProbe {
    static void runtime(const std::filesystem::path& root,const char* name) {
        VehicleTrackCourse course;std::string error;
        if(!load_vehicle_track_course(root/"Tracks"/name,1,0,-1,0,course,error)){check(false,error.c_str());return;}
        RaceSession session(course);
        if(!session.prepare_world_motion(root,error)||!session.start(error)){check(false,error.c_str());return;}
        unsigned cars=0,engines=0,trolleys=0;
        std::vector<VehicleVec3> initial;
        for(unsigned i=0;i<session.world_objects_.size();++i) {
            const auto& b=session.world_objects_[i];initial.push_back(b.state.position);
            if(!b.animated)continue;
            const auto type=course.world_objects[i].type;
            if(type==0x427a6453U)check(b.clock.phase==float(cars++)*6.F,"wagon phase spacing");
            if(type==0x24cebc53U){++engines;check(b.clock.phase==30,"engine leads five wagons");}
            if(type==0x67dbe3eaU){++trolleys;check(b.clock.phase==0,"trolley starts at authored origin");}
            check(b.state.velocity.x==0&&b.state.velocity.y==0&&b.state.velocity.z==0,"initial velocity zero");
        }
        GamepadFrame pad{};
        for(unsigned i=0;i<180;++i)if(!session.advance(pad,error)){check(false,error.c_str());return;}
        for(unsigned i=0;i<session.world_objects_.size();++i) {
            const auto& b=session.world_objects_[i];if(!b.animated)continue;
            if(course.world_objects[i].type==0x67dbe3eaU) {
                check(b.clock.phase>0&&b.state.position.x!=initial[i].x,"public world frame advances trolley");
                check(b.state.angular_velocity.x==0&&b.state.angular_velocity.y==0&&b.state.angular_velocity.z==0,"original node angular velocity zero");
            }else check(b.state.position.x==initial[i].x&&b.clock.flags&0x80,"train remains paused before trigger");
        }
        if(engines) {
            check(cars==5&&course.train_triggers.size()==1,"authored train and trigger present");
            if(course.train_triggers.empty())return;
            const auto sphere=course.train_triggers.front();
            auto& player=const_cast<VehicleState&>(session.car());
            player.position={sphere.centre[0],sphere.centre[1],sphere.centre[2]};
            player.owner_speeds.magnitude=20;player.owner_motion.acceleration={};
            for(auto& b:session.world_objects_)if(b.animated){b.reference_ready=true;b.reference.distance=session.progress().coordinate.distance+20.F;}
            check(session.advance_world_motion(1.F/60.F,11,error),error.c_str());
            for(const auto& b:session.world_objects_)if(b.animated) {
                check(!(b.clock.flags&0x80),"trigger releases whole consist");
                if(course.world_objects[&b-session.world_objects_.data()].type==0x24cebc53U)
                    check(b.train_prediction.ready&&std::abs(b.train_prediction.delay+.2F)<.00001F,"train prediction separate from animation phase");
            }
            check(session.advance_world_motion(1.F/60.F,11,error),error.c_str());
            for(unsigned i=0;i<session.world_objects_.size();++i)if(session.world_objects_[i].animated)
                check(session.world_objects_[i].state.position.x!=initial[i].x,"released consist moves next frame");
            for(unsigned frame=0;frame<1000;++frame)check(session.advance_world_motion(1.F/60.F,11,error),error.c_str());
            for(const auto& b:session.world_objects_)if(b.animated)check(b.clock.phase<=100.F&&b.clock.phase>99.F,"train endpoint does not wrap");
        }
        if(trolleys) {
            bool wrapped=false;float last=0;
            for(unsigned frame=0;frame<2800;++frame) {
                check(session.advance_world_motion(1.F/60.F,-1,error),error.c_str());
                for(const auto& b:session.world_objects_)if(b.animated){wrapped|=b.clock.phase<last;last=b.clock.phase;}
            }
            check(wrapped,"trolley loop wraps at authored duration");
        }
        std::cout<<"runtime "<<name<<" engines="<<engines<<" cars="<<cars<<" trolleys="<<trolleys<<'\n';
    }
};
}
namespace {
void graphics(const std::filesystem::path& root,unsigned event,unsigned round) {
    using namespace nfsu;std::string error;RaceGpuScene gpu;
    gpu.set_original_draw_distance(true);gpu.set_start_scenes_enabled(false);
    if(!gpu.load(posix_file_system(),root,error,nullptr,nullptr,false,false,false,nullptr,nullptr,event,false,round)){check(false,error.c_str());return;}
    RaceRun run;
    if(!run.set_event(event,error,round)||!run.set_free_drive(true,error)||!run.start(root,gpu.track(),error)){check(false,error.c_str());return;}
    RaceGpuFrame frame;
    if(!gpu.frame(run.session(),frame,error)){check(false,error.c_str());return;}
    check(validate_draw_update(frame.commands.resource_table(),frame.commands,kDekoPreparedDrawCapacity,error),error.c_str());
    unsigned visible=0;std::uint64_t triangles=0;
    for(std::size_t i=0;i<frame.commands.draw_count();++i){
        const auto view=frame.commands.draw_view(i);const auto& draw=view.definition;triangles+=draw.index_count/3;
        if(draw.name.find("world model group ")!=0)continue;
        ++visible;bool matched=false;
        check(draw.texture_resource!=kNoHandle,"world model texture resolved");
        for(unsigned w=0;w<run.session().course().world_objects.size();++w){
            const auto& source=run.session().course().world_objects[w];
            const auto* state=run.session().world_object_state(w);
            if(!source.motion_collision||!state||source.model!=gpu.track().scene.region.meshes[draw.mesh].mesh_hash)continue;
            bool equal=true;
            for(unsigned k=0;k<3;++k)equal&=view.placement.rows[12+k]==float(state->rigid_body.position[k]);
            matched|=equal;
        }
        check(matched,"graphics and collision share animation-node pose");
    }
    check(visible>0,"live world models submitted to renderer");
    check(triangles==frame.triangles,"frame accounting includes world model triangles");
    std::cout<<"graphics event="<<event<<" round="<<round<<" world draws="<<visible<<'\n';
}
}
int main(int argc,char** argv){
    clocks();moving_contacts();arrivals();
    if(argc==2){std::filesystem::path root;std::string error;
        if(!nfsu::argument_to_path(argv[1],root,error)){std::cerr<<error<<'\n';return 2;}
        clips(root);
        nfsu::RaceWorldContactProbe::runtime(root,"TRACKB1001.lzc");
        nfsu::RaceWorldContactProbe::runtime(root,"TRACKB1201.lzc");
        graphics(root,1,0);
        graphics(root,9,1);
    }
    std::cout<<"world motion tests: "<<failures<<" failures\n";return failures?1:0;
}
