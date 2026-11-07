// PC-only closed-loop seam probe: original planner inputs -> recovered driver
// -> current track vehicle -> next planner snapshot. Optional selector3 uses
// the bounded component system; this is not a shipping complete opponent.
#include "nfsu/intro_grid.h"
#include "nfsu/ai_driver.h"
#include "nfsu/ai_action.h"
#include "nfsu/ai_powertrain.h"
#include "nfsu/player_powertrain.h"
#include <cmath>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <iostream>
using namespace nfsu;
static std::uint32_t bits(float x){std::uint32_t b;std::memcpy(&b,&x,4);return b;}
class ProbeGround final:public AiPhysicsGround {
public:
    explicit ProbeGround(const TrackContactWorld& world):world_(world){}
    bool query(const VehicleHandling& h,const PlayerRigidBodyState& b,
               std::array<AiWheelGround,4>& samples) const override {
        for(unsigned k=0;k<4;++k) {
            auto& s=samples[k];s.hub=player_strut_attachment(h,k,b);
            TrackContact hit;
            const auto status=query_track_contact_point(world_,s.hub.data(),1.F,2.F,hit);
            if(status!=TrackContactStatus::hit)return false;
            s.height=hit.point[2];std::copy_n(hit.normal,3,s.normal.begin());
        }
        return true;
    }
private:const TrackContactWorld& world_;
};
int main(int argc,char** argv) {
    if(argc!=2&&argc!=3)return 2;
    const bool fallback=argc==3&&std::string(argv[2])=="--track-fallback";
    const bool track_selector3=fallback||(argc==3&&std::string(argv[2])=="--track-selector3");
    const bool selector3=track_selector3||(argc==3&&std::string(argv[2])=="--selector3");
    if(argc==3&&!selector3)return 2;
    IntroGrid grid;std::string error;
    if(!load_intro_grid(argv[1],grid,error)){std::cerr<<error;return 1;}
    const IntroGridActor* actor=nullptr;
    for(const auto& a:grid.actors)if(a.config.visual.car=="ECLIPSE")actor=&a;
    if(!actor){std::cerr<<"Eclipse absent";return 1;}
    VehiclePose pose;
    if(!(std::cin>>pose.ground.x>>pose.ground.y>>pose.ground.z>>pose.heading>>pose.speed))return 2;
    auto spec=actor->placement_spec;spec.nitrous={};
    RaceSession session(grid.course,spec,RaceSteering::baseline,actor->hull,RacePresentation::none);
    float low_slip=1;if(selector3)std::cin>>low_slip;
    if(track_selector3&&!session.configure_ai(low_slip,error)){std::cerr<<error;return 1;}
    if(!session.start(error,&pose)){std::cerr<<error;return 1;}
    while(race_car_held(session.timing()))if(!session.advance_driver({},error)){std::cerr<<error;return 1;}
    AiPhysicsState physics;AiPowertrainTables tables;VehicleState car=session.car();
    ProbeGround ground(grid.course.world);
    if(selector3&&!track_selector3) {
        if(!player_request_shift_tables(spec.handling,tables.up,tables.down))return 3;
        physics.engine.low_slip=low_slip;
        const auto& q=car.orientation;
        physics.body.axes={{{{float(1-2*(q.y*q.y+q.z*q.z)),float(2*(q.x*q.y+q.w*q.z)),float(2*(q.x*q.z-q.w*q.y))}},
            {{float(2*(q.x*q.y-q.w*q.z)),float(1-2*(q.x*q.x+q.z*q.z)),float(2*(q.y*q.z+q.w*q.x))}},
            {{float(2*(q.x*q.z+q.w*q.y)),float(2*(q.y*q.z-q.w*q.x)),float(1-2*(q.x*q.x+q.y*q.y))}}}};
        physics.body.position={{float(car.position.x),float(car.position.y),float(car.position.z)}};
        physics.body.velocity={{float(car.velocity.x),float(car.velocity.y),float(car.velocity.z)}};
        for(unsigned k=0;k<3;++k)physics.body.momentum[k]=physics.body.velocity[k]*spec.handling.mass;
        physics.body.alternate_yaw=true;physics.body.yaw_torque_limit=spec.handling.yaw_torque_limit;
        physics.engine.rpm=float(car.engine_rpm);physics.engine.gear=car.gear;
        for(unsigned k=0;k<4;++k) {
            physics.engine.wheels[k].spin=float(car.wheels[k].spin);
            physics.engine.wheels[k].contact=true;
        }
    }
    const auto publish=[&] {
        const auto& s=selector3&&!track_selector3?car:session.car();const auto& q=s.orientation;
        VehicleVec3 forward{1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y+q.w*q.z),2*(q.x*q.z-q.w*q.y)};
        VehicleVec3 left{2*(q.x*q.y-q.w*q.z),1-2*(q.x*q.x+q.z*q.z),2*(q.y*q.z+q.w*q.x)};
        VehicleVec3 up{2*(q.x*q.z+q.w*q.y),2*(q.y*q.z-q.w*q.x),1-2*(q.x*q.x+q.y*q.y)};
        if(selector3&&!track_selector3) {
            const auto& a=physics.body.axes;forward={a[0][0],a[0][1],a[0][2]};
            left={a[1][0],a[1][1],a[1][2]};up={a[2][0],a[2][1],a[2][2]};
        }
        const float fwd=float(s.velocity.x*forward.x+s.velocity.y*forward.y+s.velocity.z*forward.z);
        const float mag=float(vehicle_sqrt(s.velocity.x*s.velocity.x+s.velocity.y*s.velocity.y+s.velocity.z*s.velocity.z));
        std::cout<<std::setprecision(17)<<"STATE "<<s.step;
        for(const auto& v:{s.position,s.velocity,forward,left,up})std::cout<<' '<<float(v.x)<<' '<<float(v.y)<<' '<<float(v.z);
        std::cout<<' '<<track_angle(float(forward.x),float(forward.y))<<' '<<fwd<<' '<<mag;
        for(const auto& w:s.wheels)std::cout<<' '<<w.contact<<' '<<w.prepared_slip;
        std::cout<<' '<<session.telemetry().resets<<' '<<session.work().camera_updates<<' '<<session.work().record_encodes<<std::endl;
    };
    AiDriverForwardState driver;publish();unsigned count;
    double physics_ms=0;unsigned physics_frames=0;
    while(std::cin>>count) {
        AiDriverForwardInput in;
        AiActionSpeedState action;
        if(fallback) {
            AiNeighborhood neighbors;std::array<AiActionNeighborState,16> actors{};
            int category;float gap_limit,desired_gap;
            neighbors.count=count;
            if(count>neighbors.entries.size())return 2;
            std::cin>>neighbors.self_index>>category>>gap_limit>>desired_gap>>action.speed>>action.duration;
            for(unsigned j=0;j<count;++j) {
                auto& e=neighbors.entries[j];auto& a=actors[j];e.id=a.id=j+1;
                std::cin>>e.distance>>e.lateral>>e.half_extent[0];
                for(float& v:e.lane_lateral)std::cin>>v;
                std::cin>>a.race_actor>>a.role;for(float& v:a.velocity)std::cin>>v;
            }
            if(!std::cin||!ai_action_fallback_speed(neighbors,category,actors.data(),count,gap_limit,desired_gap,action))return 3;
            std::cout<<"ACTION "<<bits(action.speed)<<' '<<bits(action.duration)<<' '<<bits(action.auxiliary)
                <<' '<<action.route_speed<<' '<<action.enabled<<' '<<action.catchup<<' '<<action.ready<<std::endl;
            std::cin>>count;
        }
        if(count>in.path.points.size())return 2;
        in.path.count=static_cast<std::uint8_t>(count);
        std::cin>>in.heading>>in.forward_speed>>in.absolute_speed>>in.desired_speed>>in.category>>in.enabled>>in.race_seconds;
        for(float& v:in.position)std::cin>>v;
        for(float& v:in.velocity)std::cin>>v;
        for(unsigned i=0;i<count;++i)for(float& v:in.path.points[i])std::cin>>v;
        for(float& v:in.wheel_slip)std::cin>>v;
        for(auto& v:in.contact)std::cin>>v;
        std::cin>>in.rear_drive_fraction>>in.route_distance>>in.throttle_cap;
        if(!std::cin)return 2;
        // Follow the portable action's explicit speed all the way to physical
        // controls. Route-speed frames still use the native road publication
        // supplied by this seam probe, not a claim of a complete port planner.
        if(fallback&&!action.route_speed)in.desired_speed=action.speed;
        if(!ai_driver_ordinary_step(in,float(kDriveFrameSeconds),driver)) {
            std::cout<<"UNSUPPORTED ordinary driver context"<<std::endl;return 3;
        }
        VehicleInput controls;controls.throttle=driver.pedals.throttle;controls.brake=driver.pedals.brake;
        controls.road_steering=true;controls.road_angle_word_valid=true;controls.road_angle_word=driver.steering.word;
        controls.road_angle=double(static_cast<std::int16_t>(driver.steering.word))*(6.28318530717958647692/65536);
        controls.road_target=controls.road_angle;
        std::cout<<"DRIVER "<<driver.steering.word<<' '<<bits(driver.pedals.throttle)<<' '<<bits(driver.pedals.brake)<<std::endl;
        if(selector3&&!track_selector3) {
            physics.engine.throttle=float(controls.throttle);physics.engine.brake=float(controls.brake)*.5F;
            physics.engine.steering=driver.steering.word;
            physics.engine.gear_request=driver.gear_request;
            physics.engine.world_time=static_cast<std::int32_t>((car.step*4000)/60);
            physics.aero.processed_throttle=physics.engine.throttle;
            unsigned support=0;for(const auto& w:physics.engine.wheels)support+=w.contact?1U:0U;
            physics.aero.support_kind=float(support)*.25F;
            const auto preloads=player_aero_preloads(spec.handling,in.forward_speed,true);
            for(unsigned k=0;k<4;++k)physics.engine.wheels[k].preload=preloads[k];
            const auto started=std::chrono::steady_clock::now();
            const bool advanced=ai_physics_advance(spec.handling,tables,spec.aero,float(kDriveFrameSeconds),ground,physics);
            physics_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            ++physics_frames;
            if(!advanced) {
                std::cerr<<"selector3 component/ground boundary refused at "<<car.step;return 3;
            }
            const auto& b=physics.body;car.position={b.position[0],b.position[1],b.position[2]};
            car.velocity={b.velocity[0],b.velocity[1],b.velocity[2]};
            car.angular_velocity={b.angular_velocity[0],b.angular_velocity[1],b.angular_velocity[2]};
            for(unsigned k=0;k<4;++k) {
                car.wheels[k].spin=physics.engine.wheels[k].spin;
                car.wheels[k].contact=physics.engine.wheels[k].contact;
                car.wheels[k].prepared_slip=physics.engine.wheels[k].slip[0];
            }
            car.engine_rpm=physics.engine.rpm;car.gear=physics.engine.gear;++car.step;
        } else {
            const auto started=std::chrono::steady_clock::now();
            if(!session.advance_driver(controls,error,driver.gear_request)){std::cerr<<error;return 1;}
            physics_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            ++physics_frames;
        }
        publish();
    }
    if(track_selector3)physics=session.ai_physics();
    if(selector3)std::cout<<"WORK frames "<<physics_frames<<" slices "<<physics.slices
        <<" ground_queries "<<physics.ground_queries<<" physics_ms "<<physics_ms<<'\n';
    return 0;
}
