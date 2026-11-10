#include "nfsu/race_audio.h"
#include "nfsu/arg_path.h"
#include <cmath>
#include <iostream>
#include <algorithm>
namespace {
unsigned failures=0;
void check(bool okay,const char* message){if(!okay){std::cerr<<"FAIL "<<message<<'\n';++failures;}}
nfsu::VehicleHull cube(){
    nfsu::VehicleHull h;
    for(unsigned i=0;i<8;++i)h.points.push_back({i&1?-1.F:1.F,i&2?-1.F:1.F,i&4?-1.F:1.F});
    return h;
}
nfsu::VehicleSpec spec(){
    auto s=nfsu::stock_240sx_vehicle_spec();s.handling.mass=2;s.handling.inertia={1,1,1};
    s.handling.centre_of_mass={};s.handling.alternate_yaw_scale=1;
    s.handling.wheels[0].lateral=-.7F;s.handling.wheels[1].lateral=.7F;return s;
}
void native_cases(){
    using namespace nfsu;
    struct Case {float speed,side;unsigned initial;bool world_first;PlayerRigidMatrix axes;
        ContactVector momentum,angular;unsigned state,gate;float magnitude;unsigned seed;};
    const Case cases[]={
#include "world_object_contact_native.inc"
    };
    const auto setup=spec();const auto box=race_contact_box(cube());unsigned index=0;
    for(const auto& c:cases){
        VehicleState car,object;car.orientation=object.orientation={1,0,0,0};car.velocity={c.speed,0,0};
        object.position={1.9F,c.side,.2F};car.collision_state=c.initial;car.published_support=4;
        car.alternate_yaw_active=object.alternate_yaw_active=false;
        vehicle_import_rigid(setup.handling,car);vehicle_import_rigid(setup.handling,object);
        car.rigid_body.axes=c.axes;car.rigid_body.snapshot_axes=c.axes;
        ContactRecord record;unsigned seed=12345;RaceContactContext context{record,seed,5};
        RaceContactJob job;job.admitted=true;job.query.witness_a=job.query.witness_b={.95F,c.side*.5F,.1F};
        job.actor_up[c.world_first?1:0]=c.axes[2];job.actor_up[c.world_first?0:1]={0,0,1};
        RacePairContact result;auto& a=c.world_first?object:car;auto& b=c.world_first?car:object;
        const bool okay=resolve_swept_race_pair(a,setup,box,b,setup,box,result,race_contact_pose(a),
            race_contact_pose(b),&context,&job,{c.world_first,!c.world_first});
        double error=std::abs(double(record.magnitude_sum)-c.magnitude);
        for(unsigned k=0;k<3;++k){error+=std::abs(double(car.rigid_body.momentum[k])-c.momentum[k]);
            error+=std::abs(double(car.rigid_body.angular_momentum[k])-c.angular[k]);}
        if(!okay||!std::isfinite(error)||error>.00002||car.collision_state!=c.state||
           unsigned(record.state_gate)!=c.gate||seed!=c.seed){
            std::cerr<<"native case "<<index<<" error="<<error<<" seed="<<seed<<" expected="<<c.seed<<'\n';++failures;
        }
        check(object.position.x==double(1.9F)&&object.velocity.x==0&&object.rigid_body.momentum==ContactVector{},
              "unbacked WorldObject is immutable under impact");
        ++index;
    }
}
void fixed_prop_separation(){
    using namespace nfsu;
    const auto setup=spec();const auto box=race_contact_box(cube());
    VehicleState fixed,prop;fixed.orientation=prop.orientation={1,0,0,0};
    prop.position={1.5,.4,0};
    RaceContactJob stationary,fast;
    const auto search=[&](RaceContactJob& result){
        return search_race_contact(fixed,setup,box,prop,setup,box,race_contact_pose(fixed),
            race_contact_pose(prop),result,nullptr,RaceContactPartner::prop,RaceContactPartner::fixed_world);
    };
    check(search(stationary),"fixed object and stationary prop contact search");
    prop.velocity={-40,0,0};
    check(search(fast),"fixed object and fast prop contact search");
    check(stationary.admitted&&fast.admitted&&stationary.correction.y>0&&
          fast.correction.x==stationary.correction.x&&fast.correction.y==stationary.correction.y,
          "a fixed object uses centre separation independent of a prop's speed");
}
}
namespace nfsu {
struct RaceWorldContactProbe {
    static bool props(double direction,std::string& error){
        VehicleTrackCourse course;if(!build_vehicle_proving_ground(course,error))return false;
        VehicleWorldObject source;source.type=0x3e83cbde;source.fixed_collision=true;source.hull=cube();
        source.transform={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        source.transform[12]=float(64+direction*1.9);source.transform[13]=48;source.transform[14]=30;
        course.world_objects.push_back(source);
        RaceSession session(course);if(!session.start(error))return false;
        session.timing_.now_ticks=10000;
        session.props_=std::make_unique<RaceProps>();auto& props=*session.props_;
        RaceProp p;p.spec=spec();p.points=cube().points;p.box=race_contact_box(cube());
        p.bounds=race_track_body_bounds(p.spec,p.box);p.state.position={64,48,30};p.state.velocity={10*direction,0,0};
        p.state.orientation={1,0,0,0};p.state.alternate_yaw_active=false;
        vehicle_import_rigid(p.spec.handling,p.state);p.history=race_contact_pose(p.state);
        p.proxy_id=-1;p.body_id=11;p.world_slot=0;p.moved=p.awake=true;p.impact={0,{.1F,.3F}};
        props.objects_.push_back(p);session.world_shape_ids_.assign(1,true);
        std::vector<RaceQueuedContact> trace;
        if(!session.resolve_world_contacts({race_contact_pose(session.car())},{},error,nullptr,nullptr,&trace))return false;
        const auto& result=props.objects_[0];
        return std::any_of(trace.begin(),trace.end(),[](const auto& hit){return (hit.a==10&&hit.b==42)||(hit.a==42&&hit.b==10);})&&
            result.state.velocity.x*direction<10&&result.contact_count&&session.world_objects_[0].state.position.x==source.transform[12]&&
            std::isfinite(result.state.position.z)&&!result.expired;
    }
    static bool loaded_pose(const VehicleTrackCourse& course){
        RaceSession session(course);
        for(std::size_t i=0;i<course.world_objects.size();++i){
            const auto& source=course.world_objects[i];const auto& body=session.world_objects_[i];
            if(body.enabled!=source.fixed_collision)return false;
            if(!body.enabled)continue;
            const auto axes=race_contact_axes(body.state);
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)if(axes[r][c]!=source.transform[4*r+c])return false;
            if(body.state.position.x!=source.transform[12]||body.state.position.y!=source.transform[13]||body.state.position.z!=source.transform[14])return false;
        }
        return true;
    }
    static bool run(double direction,bool recovering,std::string& error){
        VehicleTrackCourse course;if(!build_vehicle_proving_ground(course,error))return false;
        VehicleWorldObject source;source.type=0x3e83cbde;source.name=1;source.shape=1;
        source.fixed_collision=true;source.hull=cube();source.transform={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        source.transform[12]=float(64+direction*1.9);source.transform[13]=48;source.transform[14]=30;
        course.world_objects.push_back(source);
        RaceSession session(course,spec(),RaceSteering::baseline,cube());if(!session.start(error))return false;
        auto car=session.car();car.position={64,48,30};car.velocity={20*direction,2,0};car.orientation={1,0,0,0};
        car.simulation_seconds=kDriveFrameSeconds;car.rigid_body_ready=false;car.alternate_yaw_active=false;
        if(!session.vehicle_.start(car))return false;
        session.timing_.now_ticks=10000;
        if(recovering){session.vehicle_.owner_recovery_start_=9900;
            if(!session.actor_recovering(0))return false;}
        auto history=race_contact_pose(session.car());history.time=0;history.position.x-=direction*.3;
        std::vector<RaceQueuedContact> trace;
        if(!session.resolve_world_contacts({history},{},error,nullptr,nullptr,&trace))return false;
        const auto& body=session.world_objects_[0];
        const bool pose=body.state.position.x==source.transform[12]&&body.state.position.y==48&&
            body.state.position.z==30&&body.state.velocity.x==0&&body.state.rigid_body.axes[0][0]==1;
        const bool observed=std::any_of(trace.begin(),trace.end(),[](const auto& c){return c.a>=42||c.b>=42;});
        const bool slower=session.car().velocity.x*direction<20;
        const bool sound=session.pair_frame_.count&&session.pair_frame_.pairs[0].other_kind==4;
        if(!pose||!observed||!slower||!sound){std::cerr<<"queue "<<direction<<' '<<recovering<<" pose="<<pose
            <<" trace="<<trace.size()<<" speed="<<session.car().velocity.x<<" sounds="<<session.pair_frame_.count<<'\n';return false;}
        for(const auto& pair:session.world_contact_pairs_)if(pair.track()&&pair.a>=42)return false;
        // Match the normal post-advance audio snapshot boundary.
        ++session.timing_.step;session.track_telemetry_.back().step=4;
        const auto audio=audio::race_audio_snapshot(session);
        return audio.contact_count&&audio.contacts[0].other_kind==4&&audio.contacts[0].point_valid&&
            audio.contacts[0].slide_mps>0&&audio.contacts[0].slide_mps==session.pair_frame_.pairs[0].slide_mps;
    }
};
}
int main(int argc,char** argv){
    native_cases();fixed_prop_separation();std::string error;
    for(double d:{-1.,1.})for(bool ghost:{false,true})
        check(nfsu::RaceWorldContactProbe::run(d,ghost,error),error.empty()?"shared kind4 queue including recovering car":error.c_str());
    for(double d:{-1.,1.})check(nfsu::RaceWorldContactProbe::props(d,error),
        error.empty()?"shared prop versus fixed WorldObject queue":error.c_str());
    if(argc==2){
        using namespace nfsu;std::filesystem::path root;
        if(!argument_to_path(argv[1],root,error)){std::cerr<<error;return 2;}
        for(int direction:{0,1})for(const char* name:{"TRACKB1001.lzc","TRACKB1201.lzc"}){
            VehicleTrackCourse course;
            check(load_vehicle_track_course(root/"Tracks"/name,1,direction,-1,0,course,error),error.c_str());
            check(RaceWorldContactProbe::loaded_pose(course),"loaded WorldObject pose retains authored rotation and scale");
            unsigned fixed=0,jumps=0,defaults=0,driven=0;
            for(const auto& object:course.world_objects){
                if(object.fixed_collision){++fixed;check(!object.hull.points.empty(),"fixed kind4 hull loaded");}
                else ++driven;
                jumps+=object.name==0x6691470d;defaults+=object.default_shape;
            }
            if(std::string(name)=="TRACKB1001.lzc")check(fixed==4&&driven==1&&defaults==1,"named parked objects and separate Trolley controller");
            else check(jumps==unsigned(direction==0)&&fixed==unsigned(direction==0)&&driven==6,
                       "native reverse jump exclusion and separate moving train classes");
        }
    }
    std::cout<<"world object contact tests: "<<failures<<" failures\n";return failures?1:0;
}
