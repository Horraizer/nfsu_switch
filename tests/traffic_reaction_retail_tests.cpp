#include "nfsu/intro_grid.h"
#include "nfsu/race_traffic.h"
#include "nfsu/arg_path.h"
#include "nfsu/ai_path_choice.h"
#include <cmath>
#include <iostream>
using namespace nfsu;
namespace {
// Exercise the ordinary mover on a sloped retail road. Its chassis uses the
// previous heading to sample the four hubs before publishing the new velocity.
bool check_ordinary_ground(const std::filesystem::path& root,const VehicleTrackCourse& course,
                           const RacingLineTrack& lines,const RaceTrafficSetup& setup,std::string& error) {
    RaceTraffic traffic;
    if(!traffic.load(root,course,{setup},error))return false;
    VehicleModel model(setup.spec);
    const auto& h=setup.spec.handling;
    for(const auto& route:lines.routes) {
        if(route.index<0||route.index>99||!route.lane_enabled[0])continue;
        for(unsigned i=1;i+1<route.points.size();++i) {
            const auto& p=route.points[i];
            if(std::abs(p.unknown_30)<.015F)continue;
            VehiclePose pose;
            pose.ground={p.position[0],p.position[1],p.height};
            pose.heading=std::atan2(p.forward[1],p.forward[0]);pose.speed=12;
            auto& a=traffic.contact_actors().front();
            if(!model.reset(a.car,pose))return false;
            a.active=true;a.registration=1;a.impacted=false;a.lane=0;
            a.road.selected=true;a.cursor=a.road.road={route.index,std::uint16_t(i)};a.road.category=11;
            a.motion={float(pose.ground.x),float(pose.ground.y),float(pose.heading),12};
            const auto before=a.car.position;
            const auto heading=track_angle(p.forward[0],p.forward[1]);
            a.published_heading=heading;
            VehicleState observer=a.car;
            observer.position.x+=100;
            const auto box=race_contact_box(setup.hull);
            if(!traffic.prepare_frame({{&observer}},2,error,4000,1.F/60.F,{},{{&box}},true))return false;
            if(!traffic.advance_body(0,error))return false;
            traffic.finish_frame();
            if(!a.active||a.impacted)continue;
            VehicleVec3 expected{a.motion.x,a.motion.y,before.z};
            PlayerRigidMatrix axes;
            const auto hubs=vehicle_placement_hubs(h,heading,expected,axes);
            TrackVehicleGround ground(course.world,true,TrackStrutQuery::cached_height,&course.support_faces);
            std::array<VehicleTrackCast,4> casts;
            ground.probe_strut_heights(hubs,casts);
            std::array<VehicleGroundHit,4> hits;
            for(unsigned k=0;k<4;++k)hits[k]=casts[k].hit;
            if(!vehicle_placement_ground(h,hubs,hits,expected,axes)||std::abs(axes[0][2])<.012F)continue;
            std::cout<<"TRAFFIC_SLOPE route="<<route.index<<" point="<<i
                     <<" pitch_axis_z="<<axes[0][2]<<" z_error="<<a.car.position.z-expected.z<<'\n';
            return a.car.position.z==expected.z && a.car.rigid_body.axes==axes &&
                   a.car.rigid_body.snapshot_axes==axes && a.car.owner_motion.axes==axes;
        }
    }
    error="no ordinary traffic on supported grade";
    return false;
}
}
int main(int argc,char** argv){
    std::string error;if(argc!=2||!check_arguments(argc,argv,1,error)){std::cerr<<"FAIL retail root\n";return 1;}
    const auto root=argument_path(argv[1]);IntroGrid grid;
    if(!load_intro_grid(root,grid,error)||grid.traffic.empty()){std::cerr<<"FAIL "<<error<<'\n';return 1;}
    const auto& car=grid.traffic.front();const RaceTrafficSetup setup{car.placement_spec,car.hull,car.assets.car_type_name};
    VehicleModel model(setup.spec);const auto box=race_contact_box(setup.hull);
    std::vector<unsigned char> image;const auto track=load_race_route_track(root/"Tracks"/grid.course.name,image);RacingLineTrack lines;
    if(!track.ok()||!parse_racing_line_image(image.data(),image.size(),{},lines)){std::cerr<<"FAIL roads\n";return 1;}
    if(!check_ordinary_ground(root,grid.course,lines,setup,error)){std::cerr<<"FAIL four-wheel traffic placement "<<error<<'\n';return 1;}
    bool avoided=false,intersection=false,retained_checked=false;unsigned attempts=0;
    for(const auto& route:lines.routes) {
        if(route.index<0||route.index>99)continue;
        unsigned lane=6,enabled=0;for(unsigned j=0;j<6;++j)if(route.lane_enabled[j]){if(lane==6)lane=j;++enabled;}
        if(lane==6||enabled<2)continue;
        for(unsigned point=10;point+10<route.points.size()&&attempts<128;point+=13) {
            const auto& p=route.points[point];if(!ai_traffic_spawn_point_allowed(p.unknown_84,true))continue;
            if(!retained_checked&&p.unknown_24>1.F&&p.unknown_28>1.F) {
                RaceTraffic probe;std::uint32_t seed=0x60000000U;
                if(!probe.load(root,grid.course,{setup},error,1,&seed)){std::cerr<<"FAIL "<<error<<'\n';return 1;}
                auto& body=probe.contact_actors()[0];
                body.active=true;body.registration=1;body.lane=lane;
                body.road.selected=true;body.road.road={route.index,std::uint16_t(point)};
                body.action_owner.current.kind=7;
                AiPathActorSnapshot actor;actor.position={p.position[0],p.position[1],p.height};
                actor.forward={p.forward[0],p.forward[1],0};actor.heading=track_angle(p.forward[0],p.forward[1]);
                auto& raw=body.driver_path;raw.path.count=2;raw.length=100;
                for(unsigned j=0;j<2;++j)raw.path.points[j]={actor.position[0]+p.forward[0]*100.F*float(j+1),
                    actor.position[1]+p.forward[1]*100.F*float(j+1),p.height};
                const auto same=[](const AiPlannedPath& a,const AiPlannedPath& b) {
                    return a.path.count==b.path.count&&a.path.points==b.path.points&&a.length==b.length&&
                        a.cursor==b.cursor&&a.cursor_tick==b.cursor_tick&&a.prediction_tick==b.prediction_tick;
                };
                if(!probe.recycle_before_controllers({{&body.car}},{},{},1,4000,1.F/60.F,true,false,nullptr,error)) {
                    std::cerr<<"FAIL retained publication "<<error<<'\n';return 1;
                }
                const auto retained=raw;for(auto& v:raw.path.points)v[0]+=400;
                // Original57A530 has already copied the Driver.5832A0 does
                // not kill that Actor identity or its cached neighbor entry.
                body.active=false;body.registration=2;AiPlannedPath result;
                if(!probe.world_prediction(0,actor,4000,.75F,result)||!same(result,retained)) {
                    std::cerr<<"FAIL parked traffic lost its retained raw path\n";return 1;
                }
                body.active=true;actor.position[0]+=p.forward[0];actor.position[1]+=p.forward[1];
                if(!probe.world_prediction(0,actor,4000,.75F,result)||!same(result,retained)) {
                    std::cerr<<"FAIL reused traffic replaced the epoch path\n";return 1;
                }
                if(!probe.recycle_before_controllers({{&body.car}},{},{},2,4067,1.F/60.F,true,false,nullptr,error)||
                   !probe.world_prediction(0,actor,4067,.75F,result)||!same(result,raw)||
                   !probe.world_prediction(0,actor,4067,.75F,result)||!same(result,raw)) {
                    std::cerr<<"FAIL retained query confused current and cached epochs\n";return 1;
                }
                // The late457846 outside-road arm uses live body motion,
                // while the neighbor entry outlives its original publication epoch.
                actor.position[0]=p.position[0]+p.forward[1]*(p.unknown_28+10.F);
                actor.position[1]=p.position[1]-p.forward[0]*(p.unknown_28+10.F);
                if(!probe.world_prediction(0,actor,4067,.75F,result)||same(result,raw)) {
                    std::cerr<<"FAIL relocated traffic skipped live outside-road forecast\n";return 1;
                }
                retained_checked=true;
                std::cout<<"PASS retained traffic path survives park/reuse; new epochs and live outside-road forecast\n";
            }
            for(unsigned mode=0;mode<2;++mode) {
                if(mode?intersection:avoided)continue;
                ++attempts;std::uint32_t random=0x60000000U;RaceTraffic traffic;
                if(!traffic.load(root,grid.course,{setup},error,1,&random)){std::cerr<<"FAIL "<<error<<'\n';return 1;}
                auto& a=traffic.contact_actors()[0];const float offset=p.lane_offset[lane];
                VehiclePose pose;pose.ground={p.position[0]+p.forward[1]*offset,p.position[1]-p.forward[0]*offset,p.height+p.unknown_2c*offset};
                pose.heading=std::atan2(p.forward[1],p.forward[0]);pose.speed=20;
                if(!model.reset(a.car,pose)){std::cerr<<"FAIL placement\n";return 1;}
                a.active=true;a.registration=1;a.lane=lane;a.cursor={route.index,std::uint16_t(point)};
                a.road.selected=true;a.road.road=a.cursor;a.road.category=11+int(lane);
                a.motion={float(a.car.position.x),float(a.car.position.y),float(pose.heading),20};
                a.published_heading=a.car.owner_motion.heading;a.target_speed=20;
                a.action_owner.current={1,8,0,false,0};a.action_records[0].owned=a.action_owner.current;a.next_action_handle=2;
                VehicleState observer;auto observer_pose=pose;observer_pose.speed=0;
                observer_pose.ground.x+=p.forward[0]*(mode?1000:12);observer_pose.ground.y+=p.forward[1]*(mode?1000:12);
                if(!model.reset(observer,observer_pose)){std::cerr<<"FAIL observer placement\n";return 1;}
                const std::array<const VehicleState*,4> racers{{&observer}};
                const std::array<const RaceContactBox*,4> shapes{{&box}};
                const auto before=a.car.position;const auto before_time=a.car.simulation_seconds;
                if(!traffic.prepare_frame(racers,1,error,4000,1.F/60.F,{},shapes)) {
                    std::cerr<<"FAIL reaction preparation "<<route.index<<'/'<<point<<' '<<error<<'\n';return 1;
                }
                // Selector promotion may round a cached pose to float at dt0.
                if(std::abs(a.car.position.x-before.x)>1e-4||std::abs(a.car.position.y-before.y)>1e-4||std::abs(a.car.position.z-before.z)>1e-4||a.car.simulation_seconds!=before_time) {
                    std::cerr<<"FAIL controller phase integrated a traffic body "<<before.x<<","<<before.y<<","<<before.z<<" -> "<<a.car.position.x<<","<<a.car.position.y<<","<<a.car.position.z<<" time="<<before_time<<"/"<<a.car.simulation_seconds<<" impacted="<<a.impacted<<'\n';return 1;
                }
                if(!traffic.advance_body(0,error)){std::cerr<<"FAIL traffic integration "<<error<<'\n';return 1;}
                traffic.finish_frame();
                if(!a.active||a.action_owner.current.kind!=(mode?18:3))continue;
                if(a.path.count<2||!std::isfinite(a.target_speed)||a.road.category<11||a.road.category>16){std::cerr<<"FAIL native action publication\n";return 1;}
                if(!mode&&(!a.impacted||a.action_owner.physical_selector!=3)){std::cerr<<"FAIL avoidance did not promote the real body\n";return 1;}
                if(mode) {
                    const RaceTrafficActionRecord* current=nullptr;
                    for(const auto& r:a.action_records)if(r.owned.handle==a.action_owner.current.handle)current=&r;
                    if(!current||current->stop.intersection<0||current->stop.mode!=1){std::cerr<<"FAIL authored intersection state\n";return 1;}
                }
                const auto decisions=traffic.decisions;const auto seed=random;
                if(!traffic.advance(racers,2,error,4067,1.F/60.F,{},shapes)||traffic.decisions!=decisions||random!=seed){std::cerr<<"FAIL retained native cadence "<<error<<'\n';return 1;}
                if(!traffic.advance(racers,3,error,4133,1.F/60.F,{},shapes)){std::cerr<<"FAIL next native action step "<<error<<'\n';return 1;}
                std::cout<<"REACTION "<<(mode?"intersection18":"avoidance3")<<" route="<<route.index<<" point="<<point<<" lane="<<a.road.category<<" target_speed="<<a.target_speed<<'\n';
                if(mode)intersection=true;else avoided=true;
            }
            if(avoided&&intersection)break;
        }
        if(avoided&&intersection)break;
    }
    if(!avoided||!intersection||!retained_checked){std::cerr<<"FAIL authored reaction coverage attempts="<<attempts<<" avoid="<<avoided<<" intersection="<<intersection<<'\n';return 1;}
    std::cout<<"PASS production traffic reactions, real road/shape/driver/intersection bindings and retained cadence\n";return 0;
}
