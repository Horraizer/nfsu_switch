#include "nfsu/vehicle_track.h"
#include "nfsu/race_contacts.h"
#include "nfsu/player_powertrain.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
using namespace nfsu;
namespace {
unsigned failures=0;
void check(bool value,const char* label) {if(!value){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
}
int main() {
#include "ai_path_body_cases.h"
    {
        struct PlacementCase {
            unsigned heading,missing;
            std::array<float,4> heights;
            PlayerRigidVector position;
            float axes[3][3];
            float hubs[4][3];
        };
#include "ai_placement_native.inc"
        for(const auto& c:placement_cases) {
            VehicleVec3 position{12.5,-4.25,3};PlayerRigidMatrix axes;
            const auto h=stock_240sx_handling();
            const auto hubs=vehicle_placement_hubs(h,std::uint16_t(c.heading),position,axes);
            std::array<VehicleGroundHit,4> hits{};
            for(unsigned k=0;k<4;++k) {
                const float actual[]={float(hubs[k].x),float(hubs[k].y),float(hubs[k].z)};
                for(unsigned j=0;j<3;++j)check(std::abs(actual[j]-c.hubs[k][j])<2e-6F,"native placement footprint");
                hits[k].hit=!(c.missing&(1U<<k));hits[k].has_plane=true;
                hits[k].plane={0,0,1,c.heights[k]};
            }
            check(vehicle_placement_ground(h,hubs,hits,position,axes)==(c.missing==0),"native placement partial support");
            const float actual[]={float(position.x),float(position.y),float(position.z)};
            for(unsigned j=0;j<3;++j) {
                check(std::abs(actual[j]-c.position[j])<2e-6F,"native placement origin");
                for(unsigned k=0;k<3;++k)check(std::abs(axes[k][j]-c.axes[k][j])<2e-6F,"native placement axes");
            }
        }
    }
    {
        const auto handling=stock_240sx_handling();
        VehicleVec3 position{10,20,30};
        PlayerRigidMatrix axes;
        auto hubs=vehicle_placement_hubs(handling,0x4000,position,axes);
        const auto authored_axes=axes;
        hubs.fill(position);
        std::array<VehicleGroundHit,4> hits{};
        for(auto& hit:hits) {
            hit.hit=hit.has_plane=true;
            hit.plane={0,0,1,0};
        }
        check(!vehicle_placement_ground(handling,hubs,hits,position,axes) &&
              axes==authored_axes && position.x==10 && position.y==20 && position.z==30,
              "degenerate footprint retains finite authored placement");
    }
    {
        const auto stock=stock_240sx_handling();

        std::array<VehicleGroundHit,4> hits{};
        for(auto& hit:hits) {
            hit.hit=hit.has_plane=true;
            hit.plane={.08F,-.12F,1.F,2.F};
        }
        for(unsigned axles:{1U,2U,3U})for(const auto heading:{0,0x4000,0x91c3}) {
            auto mirrored=stock;
            for(unsigned k=0;k<4;++k)if(axles&(1U<<(k/2)))
                mirrored.wheels[k].lateral=-mirrored.wheels[k].lateral;
            VehicleVec3 reference{12,-4,3},position=reference;
            PlayerRigidMatrix expected,axes;
            const auto normal_hubs=vehicle_placement_hubs(stock,heading,reference,expected);
            const auto mirrored_hubs=vehicle_placement_hubs(mirrored,heading,position,axes);
            check(vehicle_placement_ground(stock,normal_hubs,hits,reference,expected) &&
                  vehicle_placement_ground(mirrored,mirrored_hubs,hits,position,axes),
                  "both wheel layouts ground on the bank");
            check(axes[2][2]>0 && std::abs(position.z-reference.z)<2e-6,
                  "mirrored wheels preserve upright ride height");
            for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)
                check(std::abs(expected[i][j]-axes[i][j])<2e-6F,
                      "mirrored wheels preserve heading and bank");
        }
    }
    // Authenticated584B10/585810 outputs; includes a rewind-interpolated
    // non-orthogonal basis and original velocity limiting after an impulse.
    static const float tensor_pins[][39]={
#include "contact_tensor_native.inc"
    };
    for(const auto& pin:tensor_pins) {
        auto h=stock_240sx_vehicle_spec().handling;
        h.mass=pin[1];for(unsigned k=0;k<3;++k)h.inertia[k]=pin[2+k];
        h.alternate_yaw_scale=pin[5];PlayerRigidMatrix axes{};
        for(unsigned k=0;k<3;++k)for(unsigned j=0;j<3;++j)axes[k][j]=pin[6+3*k+j];
        const auto tensor=player_body_inverse_tensor(h,axes,pin[0]!=0);
        bool same_tensor=true;for(unsigned k=0;k<3;++k)for(unsigned j=0;j<3;++j)
            same_tensor=same_tensor&&tensor[k][j]==pin[21+3*k+j];
        check(same_tensor,"original contact inverse tensor including rewind basis");
        ContactBody body;body.identity=1;body.inverse_mass=1.F/h.mass;body.inverse_inertia=tensor;
        for(unsigned k=0;k<3;++k){body.linear_momentum[k]=pin[15+k];body.angular_momentum[k]=pin[18+k];}
        bool refreshed=refresh_car_contact_velocity(body,h.mass,200.F);
        for(unsigned k=0;k<3;++k)refreshed=refreshed&&body.linear_momentum[k]==pin[30+k]&&
            body.cached_velocity[k]==pin[33+k]&&body.cached_angular_velocity[k]==pin[36+k];
        check(refreshed,"original contact momentum and angular cache refresh");
    }
    VehicleTrackCourse course;std::string error;
    if(!build_vehicle_proving_ground(course,error)){std::cerr<<error;return 1;}
    VehicleModel model(stock_240sx_vehicle_spec());
    static const float reconstruction[][28]={
#include "rigid_reconstruction_native.inc"
    };
    for(const auto& pin:reconstruction) {
        auto h=model.spec().handling;h.mass=pin[0];
        for(unsigned k=0;k<3;++k)h.inertia[k]=pin[1+k];
        PlayerRigidMatrix axes{};
        for(unsigned k=0;k<3;++k)for(unsigned j=0;j<3;++j)axes[k][j]=pin[4+3*k+j];
        PlayerRigidBodyState body;
        player_rigid_body_import(h,body,{{pin[13],pin[14],pin[15]}},axes,
            {{pin[16],pin[17],pin[18]}},{{pin[19],pin[20],pin[21]}});
        bool match=body.axes==axes;
        for(unsigned k=0;k<3;++k)match=match&&body.position[k]==pin[13+k]&&
            body.momentum[k]==pin[22+k]&&body.angular_momentum[k]==pin[25+k];
        check(match,"original584B80 published rigid import including non-orthogonal basis");
    }
    VehicleInput input;input.throttle=.5;input.road_steering=true;input.road_angle_word_valid=true;
    VehicleTrackTelemetry telemetry;
    VehicleTrackSession car(model,course);
    {
        auto placement=course.start;
        placement.heading=0;
        placement.speed=.1F;
        VehicleState actor;
        check(model.reset(actor,placement),"slow full-mover placement");
        actor.published_gear=kVehicleGearNeutral;
        actor.published_support=4;
        VehicleTrackSession placed(model,course);
        check(placed.start(actor),"start slow full-mover fixture");
        placed.select_rival_physics(1,12135);
        auto first=placed.state();
        check(first.velocity.x==actor.velocity.x&&std::abs(first.velocity.x-.1)<.00001&&
              first.rigid_body.velocity[0]==0&&first.rigid_body.momentum[0]==0,
              "constructor rest settles private momentum before the actor publishes");
        auto edited=first;
        vehicle_import_rigid(model.spec().handling,first);
        check(first.rigid_body.momentum[0]==0&&first.velocity.x==0,
              "first solver import keeps settled momentum");
        edited.velocity.x=2;
        vehicle_import_rigid(model.spec().handling,edited);
        check(std::abs(edited.rigid_body.velocity[0]-2)<.00001,
              "explicit velocity edit before first move is imported");
    }
    {
        // 474801 publishes the reduced body's heading through floating bATan.
        // Exercise both sides of the point where the full body's integer lookup differs.
        bool distinguished=false;
        for(double yaw:{0.114,0.115,0.116,0.117}) {
            auto pose=course.start;pose.heading=yaw;
            VehicleState initial;
            VehicleTrackSession reduced(model,course);
            if(!model.reset(initial,pose)||!reduced.configure_ai(1)||!reduced.start(initial)||
               !reduced.step_ai(input,telemetry)) {
                check(false,"reduced body heading fixture");break;
            }
            const auto& car=reduced.state();
            check(telemetry.model.simulation_seconds==car.simulation_seconds &&
                  telemetry.model.solver_dt==input.world_frame_seconds &&
                  telemetry.model.owner_speeds.magnitude==car.owner_speeds.magnitude,
                  "selector3 visual publication uses the whole frame");
            for(unsigned k=0;k<4;++k) {
                const auto& w=telemetry.model.wheels[k];
                check(w.contact==car.wheels[k].contact && w.effect_slip_velocity==car.published_wheel_slip[k],
                      "selector3 publishes the solver contact and physical slip to particle effects");
            }
            const auto& owner=reduced.state().owner_motion;
            const auto& axis=owner.axes[0];
            const auto expected=track_angle(axis[0],axis[1]);
            distinguished|=expected!=player_owner_heading({axis[0],axis[1],axis[2]});
            check(owner.heading==expected,"selector3 publishes floating-point heading");
        }
        check(distinguished,"heading fixture distinguishes selector3 from selector1");
    }
    for(bool rival:{false,true}) {
        VehicleTrackSession replacement(model,course);
        VehicleState old;
        check(model.reset(old,course.start),"replacement reset fixture");
        old.engine_rpm=5000;old.published_gear=kVehicleGearNeutral;
        old.path_powertrain_speed=37.F;
        old.engine.angular_speed=700;old.engine.cutoff_until=30;
        old.engine.heat=.8F;old.engine.heat_time=2;old.engine.overheat_since=1;
        old.engine.blown=true;old.engine.feedback_torque=4;old.engine.throttle_multiplier=.2F;
        old.transmission.clutch=.3F;old.transmission.previous_slip_rpm=300;
        old.transmission.shift_pending=true;old.transmission.shift_quality=.85F;
        old.transmission.shift_boost_since=5;old.transmission.feedback_scale=.8F;
        old.velocity={7,-3,2};old.angular_velocity={.2,-.3,.4};
        old.rigid_body.momentum={99,88,77};old.rigid_body.angular_momentum={66,55,44};
        old.rigid_body.adaptive_damping=.5F;
        old.body_rest_state=2;old.body_gravity=false;old.grip_recovery=.5F;
        for(unsigned k=0;k<4;++k) {
            auto& wheel=old.wheels[k];wheel.spin=12.F+float(k);
            wheel.compression=.03;wheel.previous_compression=.02;wheel.compression_rate=3;
            wheel.history_torque=1;wheel.spin_direction=2;wheel.brake_locked=true;wheel.contact=k!=3;
            old.published_wheel_slip[k]={6.F+float(k),-2.F};
            wheel.prepared_effect_slip={99.F,88.F};
        }
        // The two front steering matrices are still empty during the constructor
        // pass. Straight steering later publishes identity matrices instead.
        old.wheels[1].spin=-13.F;
        old.steering_word=0x1200;
        old.steering_word_ready=true;
        check((!rival||replacement.configure_ai(1))&&replacement.start(old),"replacement solver setup");
        replacement.select_rival_physics(1,12135);
        const auto& fresh=replacement.state();
        bool wheels=true;
        for(unsigned k=0;k<4;++k) {
            const auto& wheel=fresh.wheels[k];
            const float spin=float(old.wheels[k].spin);
            wheels=wheels&&wheel.spin==spin&&wheel.previous_compression==0&&
                wheel.compression_rate==0&&wheel.history_torque==0&&!wheel.brake_locked&&
                wheel.spin_direction==(spin>0?0:2);
            const float rim_speed=model.spec().handling.wheels[k].radius*spin;
            if(k<2)check(wheel.prepared_slip==rim_speed,
                "new steering matrices project zero hub velocity before the first driver command");
            else check(std::abs(wheel.prepared_slip-rim_speed)>1,
                "unsteered rear wheels project the moving hub during construction");
        }
        check(fresh.forces_prepared&&!fresh.steering_word_ready&&wheels,"new solver preserves published wheel speed without old strut or torque history");
        auto before_move=fresh;
        float refresh_dt=0;
        for(unsigned pass=0;pass<2;++pass) {
            check(replacement.refresh_contact(before_move,&fresh,refresh_dt),
                "contact refresh before the replacement mover's first Move");
            check(!before_move.steering_word_ready,
                "collision callbacks leave the first steering command for Move");
            for(unsigned k=0;k<2;++k) {
                const auto& wheel=before_move.wheels[k];
                const float rim_speed=model.spec().handling.wheels[k].radius*float(wheel.spin);
                check(wheel.prepared_slip==rim_speed,
                    "pre-Move contact refresh retains both unpublished front wheel axes");
            }
        }
        check(fresh.published_wheel_slip==old.published_wheel_slip&&
            fresh.wheels[0].prepared_effect_slip==std::array<float,2>{},
            "fresh components cannot erase previous actor wheel-slip publication");
        check(fresh.rigid_body.momentum[0]==7.F*model.spec().handling.mass&&
            fresh.rigid_body.momentum[1]==-3.F*model.spec().handling.mass&&
            fresh.rigid_body.axes==old.owner_motion.axes&&fresh.rigid_body.adaptive_damping==0&&
            fresh.body_rest_state==0&&fresh.body_gravity&&fresh.grip_recovery==1,
            "new solver reconstructs published body motion and rest history");
        auto imported=fresh;
        const auto momentum=imported.rigid_body.momentum,angular=imported.rigid_body.angular_momentum;
        imported.alternate_yaw_active=true;vehicle_import_rigid(model.spec().handling,imported);
        check(imported.rigid_body.momentum==momentum&&imported.rigid_body.angular_momentum==angular,
            "first outer alternate-yaw selection cannot reimport rounded velocity caches");
        check(fresh.engine.angular_speed==5000.F*kPlayerRpmToRadS&&fresh.engine_rpm==5000&&
            fresh.path_powertrain_speed==37.F&&fresh.engine.cutoff_until==0&&fresh.engine.feedback_torque==0&&fresh.engine.throttle_multiplier==1&&
            fresh.engine.heat==0&&!fresh.engine.blown&&fresh.engine.overheat_since==0&&
            fresh.engine.heat_time==float(12135)*.00025F,
            "selector1 reconstruction imports published RPM and retires old cutoff/heat");
        check(fresh.gear==kVehicleGearNeutral&&fresh.transmission.requested_gear==kVehicleGearNeutral&&
            fresh.transmission.mode==3&&fresh.transmission.clutch==1&&
            !fresh.transmission.shift_pending&&fresh.transmission.shift_quality==0&&
            fresh.transmission.previous_slip_rpm==0&&fresh.transmission.shift_boost_since==0&&
            fresh.transmission.feedback_scale==0,
            "selector1 reconstruction starts a fresh gearbox from published neutral");
        old.engine_rpm=model.spec().handling.max_rpm;
        check(replacement.start(old),"high RPM replacement setup");
        replacement.select_rival_physics(1,12135);
        check(replacement.state().engine.cutoff_until==float(12135)*.00025F+.04F,
            "constructor refresh arms a new neutral cutoff before the first derivative");
    }
    {
        VehicleState moving;
        check(model.reset(moving,course.start),"pedal latch fixture");
        moving.gear=moving.published_gear=kVehicleGearFirst;
        moving.engine_rpm=4500;
        moving.velocity={10,0,0};
        for(auto& w:moving.wheels)w.spin=18;
        VehicleTrackSession released(model,course);
        check(released.start(moving),"coasting mover setup");
        released.select_rival_physics(1);
        const auto fresh=released.state();
        for(unsigned pedal=0;pedal<3;++pedal) {
            VehicleTrackSession held(model,course);
            VehicleInput command;
            command.throttle=pedal==0;
            command.brake=pedal==1;
            command.handbrake=pedal==2;
            VehicleTrackTelemetry t;
            check(held.start(moving)&&held.step(command,t),"driver holds pedal before contact");
            for(bool published:{false,true}) {
                auto coast=fresh,pressed=fresh;
                coast.steering_word_ready=pressed.steering_word_ready=published;
                float coast_dt=0,pressed_dt=0;
                check(released.refresh_contact(coast,&fresh,coast_dt)&&
                    held.refresh_contact(pressed,&fresh,pressed_dt),"paired pedal refresh");
                bool different=coast.engine.feedback_torque!=pressed.engine.feedback_torque;
                for(unsigned k=0;k<4;++k) {
                    const auto& a=coast.wheels[k];const auto& b=pressed.wheels[k];
                    different|=a.spin!=b.spin||a.brake_locked!=b.brake_locked||
                        a.prepared_torque!=b.prepared_torque;
                }
                check(different==published,"wheel and engine components accept pedals on first Move");
            }
        }
    }
    {
        VehicleTrackSession follower(model,course);
        VehicleState moving;
        auto pose=course.start;
        pose.heading=0;
        pose.speed=25;
        const bool prepared=model.reset(moving,pose);
        moving.velocity.y=3;
        moving.owner_speeds.forward=25;
        moving.published_support=2;
        moving.path_powertrain_speed=24.F;
        for(unsigned i=0;i<4;++i)moving.wheels[i].contact=i>=2;
        check(prepared&&follower.configure_ai(1)&&follower.start(moving),"path motion setup");
        follower.select_rival_physics(4);
        AiDriverPath path;
        path.count=5;
        for(unsigned i=0;i<path.count;++i)path.points[i]={
            float(moving.position.x)+5.F*float(i),float(moving.position.y)-.5F,float(moving.position.z)};
        auto path_input=input;
        path_input.world_frame_seconds=.01F;
        bool departure=false;
        check(follower.step_rival_path(path,25.F,0.F,true,path_input,telemetry,departure)&&!departure&&
              telemetry.position.y>moving.position.y&&telemetry.velocity.y>2.F,
              "path publication preserves the lateral velocity of the incoming body");
        auto prior=follower.state();
        AiPathPowertrainState expected_publication{24.F,moving.published_gear,float(moving.engine_rpm)};
        check(ai_path_powertrain(prior.owner_speeds.magnitude,expected_publication)&&
              prior.engine_rpm==expected_publication.rpm&&prior.published_gear==expected_publication.gear&&
              prior.path_powertrain_speed==expected_publication.filtered_speed,
              "path motion publishes original retained actor powertrain");
        check(prior.published_support==2&&!prior.wheels[0].contact&&!prior.wheels[1].contact&&
              prior.wheels[2].contact&&prior.wheels[3].contact,
              "path motion retains rear-only support instead of grounding the driver");
        bool published_slip=true;
        for(const auto& slip:prior.published_wheel_slip)
            published_slip=published_slip&&slip[1]==-float(prior.velocity.y)&&slip[1]<-2.F;
        check(published_slip,"path lateral correction publishes nonzero slip to every wheel");
        auto invalid_path=path;
        invalid_path.points[1][0]=std::numeric_limits<float>::infinity();
        check(!follower.step_rival_path(invalid_path,25.F,0.F,true,path_input,telemetry,departure)&&
              follower.state().step==prior.step&&follower.state().position.y==prior.position.y,
              "invalid path publication leaves the physical state unchanged");
        AiDriverPath crossing;
        crossing.count=5;
        for(unsigned i=0;i<crossing.count;++i)crossing.points[i]={
            float(prior.position.x),float(prior.position.y)+5.F*float(i),float(prior.position.z)};
        check(follower.step_rival_path(crossing,25.F,0.F,true,path_input,telemetry,departure)&&
              std::abs(follower.state().angular_velocity.z)<8.F,
              "crossing path retains original heading history instead of snapping or changing selector");
        prior=follower.state();
        follower.select_rival_physics(1,20000);
        check(follower.state().path_powertrain_speed==prior.path_powertrain_speed&&
              follower.state().published_gear==prior.published_gear&&
              follower.state().engine.angular_speed==std::clamp(float(prior.engine_rpm)*kPlayerRpmToRadS,
                  model.spec().handling.idle_rpm*kPlayerRpmToRadS,model.spec().handling.max_rpm*kPlayerRpmToRadS),
              "path-to-full reconstruction retains filter and imports original actor RPM");
        follower.select_rival_physics(4);
        AiDriverPath vertical;
        vertical.count=5;
        for(unsigned k=0;k<vertical.count;++k)vertical.points[k]={
            float(follower.state().position.x),float(follower.state().position.y),
            float(follower.state().position.z)+5.F*float(k)};
        check(follower.step_rival_path(vertical,25.F,0.F,true,path_input,telemetry,departure)&&
              std::isfinite(follower.state().position.z),
              "vertical spline publication uses native zero-horizontal normal instead of rejecting selector4");
    }
    {
        // A selector1 body has alternate yaw momentum.474640 must reconstruct
        // selector3 from published angular velocity using the normal tensor.
        VehicleState old;check(model.reset(old,course.start),"selector3 import fixture");
        old.angular_velocity={1.2,-1.3,.4};old.alternate_yaw_active=true;
        vehicle_import_rigid(model.spec().handling,old);
        const auto previous_momentum=old.rigid_body.angular_momentum;
        PlayerRigidBodyState native_import;
        player_rigid_body_import(model.spec().handling,native_import,
            {{float(old.position.x),float(old.position.y),float(old.position.z)}},old.owner_motion.axes,
            {{float(old.velocity.x),float(old.velocity.y),float(old.velocity.z)}},
            {{float(old.angular_velocity.x),float(old.angular_velocity.y),float(old.angular_velocity.z)}});
        VehicleTrackSession reduced(model,course);
        check(reduced.configure_ai(1)&&reduced.start(old)&&
            reduced.step_ai_body(old,input,telemetry,0,nullptr,true),"fresh selector3 refresh executes");
        check(previous_momentum!=native_import.angular_momentum&&
              reduced.ai_physics().body.angular_momentum==native_import.angular_momentum,
              "selector3 construction replaces alternate-yaw momentum with original584B80 import");
    }
    {
        //427C50 reconstructs selector4 only after successful placement.
        const auto make_path=[](const VehicleVec3& origin,bool north) {
            AiDriverPath path;path.count=5;
            for(unsigned k=0;k<path.count;++k)path.points[k]={
                float(origin.x)+(north?0.F:5.F*k),
                float(origin.y)+(north?5.F*k:-.5F),float(origin.z)};
            return path;
        };
        auto pose=course.start;pose.heading=0;pose.speed=25;
        VehicleState initial;check(model.reset(initial,pose),"retained path reset seed");
        auto controls=input;controls.world_frame_seconds=.01F;
        for(bool available:{false,true}) {
            VehicleTrackSession actual(model,course),reference(model,course);
            check(actual.configure_ai(1)&&reference.configure_ai(1)&&
                actual.start(initial)&&reference.start(initial),"retained path reset sessions");
            actual.select_rival_physics(4);reference.select_rival_physics(4);
            actual.set_road_reset_source({&available,0,
                [](void* context,unsigned,const VehicleState& state,const RaceProgress&,
                   std::int32_t,VehicleRoadResetPoint& out) {
                    if(!*static_cast<bool*>(context))return false;
                    out.position=state.position;out.forward={1,0};return true;
                },nullptr});
            VehicleTrackTelemetry a,b;bool departure=false;
            const auto path=make_path(initial.position,false);
            check(actual.step_rival_path(path,25,.5F,true,controls,a,departure)&&
                reference.step_rival_path(path,25,.5F,true,controls,b,departure),"publish before reset");
            actual.request_reset();
            check(actual.step_rival_path(path,25,.5F,false,controls,a,departure)&&
                a.reset==VehicleTrackReset::manual&&actual.last_reset().performed==available,
                "placement request distinguishes refusal from completed reset");
            if(available) {
                check(reference.start(actual.state()),"control reconstructs the placed actor");
                reference.select_rival_physics(4);
            } else check(reference.step_rival_path(path,25,.5F,false,controls,b,departure),
                "unreset control advances its retained path");
            const auto changed=make_path(actual.state().position,true);
            check(actual.step_rival_path(changed,25,.5F,available,controls,a,departure)&&
                reference.step_rival_path(changed,25,.5F,available,controls,b,departure)&&
                actual.state().owner_motion.heading==reference.state().owner_motion.heading&&
                a.position.x==b.position.x&&a.position.y==b.position.y,
                "completed reset seeds new heading history; refused reset retains old spline and channels");
        }
    }
    for(unsigned selector:{1U,3U,4U}) {
        VehicleTrackSession reset_session(model,course);
        VehicleState old;check(model.reset(old,course.start),"placement powertrain fixture");
        old.gear=kVehicleGearFirst+1;old.published_gear=old.gear;old.engine_rpm=6500;
        old.path_powertrain_speed=37.F;
        old.published_wheel_slip.fill({17.F,12.F});
        check(reset_session.configure_ai(1)&&reset_session.start(old),"placement solver fixture");
        reset_session.select_rival_physics(selector,10000);
        auto controls=input;controls.world_clock_valid=true;controls.world_clock=12345;
        std::uint32_t random=6789;controls.gearbox_world_random=&random;
        PlayerGearState expected;player_gear_seed(expected,random);
        reset_session.request_reset();
        bool stepped=false;
        if(selector==1) {
            stepped=reset_session.step(controls,telemetry)&&reset_session.step(controls,telemetry);
        } else if(selector==3) stepped=reset_session.step_ai(controls,telemetry);
        else {
            AiDriverPath path;path.count=5;
            for(unsigned k=0;k<path.count;++k)path.points[k]={float(old.position.x)+5.F*float(k),
                float(old.position.y),float(old.position.z)};
            bool departed=false;
            stepped=reset_session.step_rival_path(path,0,0,true,controls,telemetry,departed);
        }
        check(stepped&&telemetry.reset==VehicleTrackReset::manual,"all physical selectors finish placement");
        const auto& placed=reset_session.state();
        check(placed.published_gear==kVehicleGearNeutral&&placed.gear==kVehicleGearNeutral&&
              placed.engine_rpm==1000.F&&placed.published_support==4&&
              placed.published_wheel_slip==std::array<std::array<float,2>,4>{}&&
              placed.path_powertrain_speed==(selector==4?36.F:37.F),
              "464C80 resets actor gear/RPM/slip/support but retains Actor4C0");
        check(random==(selector==1?expected.rng_state:6789U),
              "only reconstructed selector1 consumes two gearbox world RNG draws at placement");
        if(selector==1)check(placed.engine.heat_time==float(controls.world_clock)*.00025F&&
            placed.transmission.requested_gear==kVehicleGearNeutral&&placed.transmission.clutch==1,
            "placement reconstructs fresh engine and gearbox at the live world clock");
    }
    {
        struct RoadResetWitness {
            bool available=true,placed=false;
            const VehicleTrackSession* session=nullptr;
            static bool resolve(void* opaque,unsigned,const VehicleState&,const RaceProgress&,std::int32_t,VehicleRoadResetPoint& out) {
                if(!static_cast<RoadResetWitness*>(opaque)->available)return false;
                out.position={32,48,10};out.forward={1,0};return true;
            }
            static void publish(void* opaque,unsigned,const VehicleState& state,const RaceProgress&,std::int32_t) {
                auto& witness=*static_cast<RoadResetWitness*>(opaque);
                witness.placed=state.gear==kVehicleGearNeutral&&state.engine_rpm==1000.F&&
                    witness.session&&witness.session->ai_physics().prepared&&
                    witness.session->ai_physics().slices==0&&witness.session->ai_physics().ground_queries==16;
            }
        } witness;
        VehicleTrackSession selected(model,course);
        check(selected.configure_ai(1)&&selected.start(course.start),"rival road reset fixture");
        witness.session=&selected;
        selected.set_road_reset_source({&witness,0,RoadResetWitness::resolve,RoadResetWitness::publish});
        selected.request_reset();
        check(selected.step_ai(input,telemetry)&&selected.state().position.x==32&&selected.state().position.y==48&&
              selected.last_reset().node==-1&&witness.placed,
              "rival uses retained controller road target and publishes only after component reconstruction");
        witness.available=false;witness.placed=false;
        const auto count=telemetry.resets;selected.request_reset();
        check(selected.step_ai(input,telemetry)&&telemetry.resets==count&&!witness.placed,
              "missing rival road never falls back to a human nearest-node teleport");
    }
    VehicleState reset_input;
    check(model.reset(reset_input,course.start),"reset actor fixture");
    reset_input.path_powertrain_speed=37.F;
    check(car.configure_ai(1)&&car.start(reset_input),"AI world starts");
    check(car.step_ai(input,telemetry),"AI first frame");
    const auto before=car.state();
    auto invalid=input;invalid.throttle=std::numeric_limits<double>::quiet_NaN();
    check(!car.step_ai(invalid,telemetry)&&car.state().step==before.step&&
          car.state().position.x==before.position.x,"invalid AI input leaves frame unchanged");
    auto timed=input;timed.world_clock_valid=true;timed.world_clock=10000;
    car.request_reset();
    check(car.step_ai(timed,telemetry)&&telemetry.reset==VehicleTrackReset::manual,
          "AI manual reset published on world tick");
    check(car.state().path_powertrain_speed==37.F,"427C50 placement retains Actor4C0 powertrain filter");
    check(car.owner_recovery_active(10000)&&car.owner_recovery_active(17999)&&
          !car.owner_recovery_active(18000),"4280A0 strict two-second reset gate");
    const auto reset=car.state();
    VehicleTrackSession fresh(model,course);
    check(fresh.configure_ai(1)&&fresh.start(reset),"reset control session");
    // Both samples must have the same owner-recovery gate. Otherwise the
    // original rest policy intentionally freezes only the fresh control car.
    auto after_recovery=input;after_recovery.world_clock_valid=true;after_recovery.world_clock=18000;
    check(car.step_ai(after_recovery,telemetry)&&fresh.step_ai(after_recovery,telemetry),"post-reset step");
    check(car.state().rigid_body.position==fresh.state().rigid_body.position&&
          car.state().rigid_body.momentum==fresh.state().rigid_body.momentum&&
          car.ai_physics().engine.wheels[0].spin==fresh.ai_physics().engine.wheels[0].spin,
          "reset removes stale prepared forces and wheel history");
    check(car.start(course.start)&&car.ai_physics().slices==0&&
          !car.owner_recovery_active(10000),"explicit restart clears AI state and recovery");
    {
        auto fast=course.start;fast.speed=32;
        VehicleTrackSession landing(model,course);
        auto controls=input;controls.world_clock_valid=true;controls.world_clock=10000;
        check(landing.configure_ai(1)&&landing.start(fast)&&landing.step_ai(controls,telemetry),
              "moving AI prepares speed-dependent strut preload");
        auto car=landing.state();
        const auto preloads=[&]() {
            std::array<float,4> values{};
            for(unsigned k=0;k<4;++k)values[k]=landing.ai_physics().engine.wheels[k].preload;
            return values;
        };
        const auto held=preloads();
        car.collision_state=1;car.collision_support_since=0;
        const auto endpoint=car;
        const auto& handling=model.spec().handling;
        const auto recovery=player_aero_preloads(handling,0,false,1);
        check(recovery!=player_aero_preloads(handling,endpoint.owner_speeds.forward,true,1),
              "crash fixture distinguishes omitted speed load");
        check(landing.step_ai_body(car,controls,telemetry,0,&endpoint,true,false,1.F/120)&&
              preloads()==held,"collision refresh retains the previous outer preload");
        check(landing.step_ai_body(car,controls,telemetry,.004F,&endpoint,false,true)&&
              !landing.ai_physics().aero.enabled&&preloads()==recovery,
              "selector3 collision continuation keeps static preload and drops speed load");
        car.published_support=4;car.collision_body_flag=0;car.collision_support_since=1.F;
        const auto restored=player_aero_preloads(handling,car.owner_speeds.forward,true);
        check(landing.step_ai_body(car,controls,telemetry)&&car.collision_state==0&&
              landing.ai_physics().aero.enabled&&preloads()==restored,
              "selector3 restores speed load once crash recovery ends");
    }
    {
        auto fast=course.start;fast.speed=12;
        auto brakes=input;brakes.throttle=0;brakes.brake=.75;brakes.handbrake=true;
        VehicleTrackSession braking(model,course);
        check(braking.configure_ai(1)&&braking.start(fast)&&braking.step_ai(brakes,telemetry)&&
              braking.ai_physics().engine.brake==.75F,"474B6E preserves service brake range");
        brakes.brake=.25;
        check(braking.step_ai(brakes,telemetry)&&braking.ai_physics().engine.brake==.5F,
              "474B51 halves parking brake input");
    }
    // Actual UG0 integration exposed a spawn kick when the first road sample
    // differs from the placement plane. Original4743D9/4743E0 refresh struts
    // twice before integrating; the first sample must seed the damper history.
    VehicleTrackSession uneven_start(model,course);auto low_pose=course.start;
    low_pose.ground.z-=.03;low_pose.speed=2;
    VehicleState uneven_actor;check(model.reset(uneven_actor,low_pose),"uneven actor seed");
    check(uneven_start.configure_ai(1)&&uneven_start.start(uneven_actor),"uneven AI placement starts");
    check(uneven_start.ai_physics().ground_queries==8&&uneven_start.ai_physics().slices==0&&
          uneven_start.ai_physics().prepared&&uneven_start.state().step==0&&
          uneven_start.state().velocity.x==uneven_actor.velocity.x&&
          uneven_start.state().velocity.y==uneven_actor.velocity.y&&uneven_start.state().velocity.z==uneven_actor.velocity.z,
          "426110 performs both full constructor refreshes before any actor publication or derivative");
    check(uneven_start.step_ai(input,telemetry)&&std::abs(uneven_start.state().velocity.z)<1&&
          uneven_start.ai_physics().ground_queries==16,
          "initial strut refresh cannot turn terrain offset into a launch impulse");

    {
        VehicleTrackSession rotated(model,course);auto airborne=course.start;
        airborne.ground.z+=20;VehicleState seed;
        check(model.reset(seed,airborne),"orientation-only edit seed");seed.published_support=0;
        check(rotated.configure_ai(1)&&rotated.start(seed),"orientation-only edit starts");
        const auto queries=rotated.ai_physics().ground_queries;
        auto edited=rotated.state();edited.orientation={0,0,0,1};
        check(rotated.step_ai_body(edited,input,telemetry)&&rotated.ai_physics().ground_queries==queries+12,
            "orientation-only external edit refreshes the first derivative before integration");
    }

    {
        VehicleTrackSession shifted(model,course);auto airborne=course.start;
        airborne.ground.z+=20;VehicleState seed;
        check(model.reset(seed,airborne),"position-only edit seed");seed.published_support=0;
        check(shifted.configure_ai(1)&&shifted.start(seed),"position-only edit starts");
        const auto queries=shifted.ai_physics().ground_queries;
        auto edited=shifted.state();edited.position.z+=5;
        vehicle_import_rigid(model.spec().handling,edited);
        check(shifted.step_ai_body(edited,input,telemetry)&&shifted.ai_physics().ground_queries==queries+12,
            "synchronized external position edit cannot retain forces from the old component position");
    }

    // Loading settles placement with the same rest exclusion as the player.
    // Otherwise a flat reset pose can sleep on its front axle before a RWD
    // driver's rear tyres contact the sloping road (campaign30/round1).
    VehicleTrackSession grid_settle(model,course);auto raised_grid=course.start;
    raised_grid.ground.z+=.08;
    check(grid_settle.configure_ai(1)&&grid_settle.start(raised_grid)&&grid_settle.prepare_grid(180),
          "AI grid placement settles before rest admission");
    check(grid_settle.state().published_support==4&&grid_settle.state().body_gravity,
          "AI grid preparation retains gravity until all struts settle");
    VehicleInput idle;idle.road_steering=true;idle.road_angle_word_valid=true;
    check(grid_settle.step_ai(idle,telemetry)&&grid_settle.state().body_rest_state==2,
          "ordinary AI rest resumes after grid preparation");

    VehicleTrackSession reversing(model,course);
    check(reversing.configure_ai(1)&&reversing.start(course.start),"recovery car starts");
    const auto initial_x=reversing.state().position.x;
    bool reversed=true;
    for(unsigned n=0;n<30;++n)if(!reversing.step_ai(input,telemetry,0)){reversed=false;break;}
    check(reversed&&reversing.state().gear==0&&reversing.state().position.x<initial_x&&
        reversing.state().owner_speeds.forward<0,"held recovery command physically reverses car");
    const auto before_bad=reversing.state().step;
    check(!reversing.step_ai(input,telemetry,7)&&reversing.state().step==before_bad,
        "unsupported AI request refuses whole frame");
    check(reversing.step_ai(input,telemetry,-1)&&reversing.state().gear>=2&&
        reversing.ai_physics().engine.gear_request==-1,"automatic request releases reverse");

    VehicleTrackSession a(model,course),b(model,course);
    auto pa=course.start,pb=course.start;pa.speed=12;pb.ground.x+=4.2;
    check(a.configure_ai(1)&&b.configure_ai(1)&&a.start(pa)&&b.start(pb),"pair starts");
    check(a.step_ai(input,telemetry)&&b.step_ai(input,telemetry),"pair first frame");
    const double old_speed=a.state().velocity.x;
    RacePairContact hit;const auto box=race_contact_box(stock_240sx_hull());
    check(a.meet(b,box,box,hit)&&hit.applied,"selector3 pair produces impulse");
    const double impact_speed=a.state().velocity.x;
    check(impact_speed<old_speed,"contact slows approaching car");
    check(a.step_ai(input,telemetry)&&a.state().velocity.x<old_speed-.1,
          "next AI frame retains contact momentum");

    for(bool ai_driver:{false,true}) {
        VehicleTrackSession moving(model,course),target(model,course);
        auto mp=course.start,tp=course.start;mp.speed=30;
        tp.ground.x+=box.high.x-box.low.x+.1;
        check((!ai_driver||moving.configure_ai(1))&&target.configure_ai(1)&&
            moving.start(mp)&&target.start(tp),"continuation pair starts");
        const auto previous_a=race_contact_pose(moving.state()),previous_b=race_contact_pose(target.state());
        bool stepped=true;
        if(ai_driver)stepped=moving.step_ai(input,telemetry);
        else for(unsigned i=0;i<4;++i)stepped=moving.step(input,telemetry,i==3?1.0/60:0,1.0/240,i==0,4)&&stepped;
        stepped=target.step_ai(input,telemetry)&&stepped;
        check(stepped,"continuation pair advances");
        auto sampled_a=moving.state(),sampled_b=target.state();RacePairContact sampled_hit;
        ContactRecord reference_record,record;std::uint32_t reference_seed=12345,seed=12345;
        RaceContactContext reference_context{reference_record,reference_seed,66},context{record,seed,66};
        const auto clock_a=moving.state().simulation_seconds,clock_b=target.state().simulation_seconds;
        const auto step_a=moving.state().step,step_b=target.state().step;
        check(resolve_swept_race_pair(sampled_a,model.spec(),box,sampled_b,model.spec(),box,
            sampled_hit,previous_a,previous_b,&reference_context)&&sampled_hit.applied&&sampled_hit.remaining_a>0,
            "continuation has an actual early contact");
        check(moving.meet(target,box,box,hit,&previous_a,&previous_b,&context)&&hit.applied,
            "driver resumes after pair impulse");
        check(moving.state().position.x!=sampled_a.position.x&&target.state().position.x!=sampled_b.position.x,
            "both bodies move beyond sampled contact epoch");
        check(moving.state().step==step_a&&target.state().step==step_b&&
            moving.state().simulation_seconds==clock_a&&target.state().simulation_seconds==clock_b,
            "continuation never adds a second world frame");
    }

    {
        RaceContactBox tetra{{0,0,0},{2,2,2},{{0,0,0},{2,0,0},{0,2,0},{0,0,2}}};
        auto spec=model.spec();spec.handling.centre_of_mass={0,0,0};
        VehicleState first,second;first.velocity={10,10,10};second.position={1.5,1.5,1.5};
        RaceHullQuery query;
        check(query_race_hulls(first,spec,tetra,second,spec,tetra,query)&&
            query.distance==1.443375587463379F&&
            query.witness_a==ContactVector{.6666667461395264F,.6666667461395264F,.6666667461395264F},
            "convex separation despite overlapping bounding boxes");
        check(resolve_race_pair(first,spec,tetra,second,spec,tetra,hit)&&!hit.overlapping&&!hit.applied,
            "empty hull space cannot create a bounding-box impact");
    }

    // contact_lifecycle_native.py executes4776C6 and the non-closing46B5E0
    // path. Record ownership is independent of accumulated impulse.
    {
        auto spec=model.spec();spec.handling.centre_of_mass={.25F,0,.28F};
        VehicleState first,second;first.velocity={-10,0,0};second.velocity={10,0,0};
        second.position={3,.7,0};ContactRecord record;std::uint32_t seed=12345;
        RaceContactContext context{record,seed,4000};RacePairContact hit;
        const auto pa=race_contact_pose(first),pb=race_contact_pose(second);
        check(resolve_swept_race_pair(first,spec,box,second,spec,box,hit,pa,pb,&context)&&
            hit.searched&&!hit.applied&&record.active&&record.magnitude_sum==0&&
            record.creation_tick==4000&&record.last_hit_tick==4000&&
            first.collision_body_flag==1&&second.collision_body_flag==1,
            "original separating job allocates a zero-impulse contact record");
    }
    for(bool ai_driver:{false,true}) {
        VehicleState crashed;check(model.reset(crashed,course.start),"collision policy placement");
        crashed.collision_state=1;crashed.published_support=4;crashed.collision_body_flag=1;
        crashed.aero_ready=true;crashed.aero.enabled=true;
        VehicleTrackSession resumed(model,course);
        check((!ai_driver||resumed.configure_ai(1))&&resumed.start(crashed),"collision policy session");
        auto controls=input;
        if(ai_driver)check(resumed.step_ai(controls,telemetry),"collision AI controls");
        // Zero remaining time still executes completion; it does not add a frame.
        const auto before=resumed.state();
        auto other_state=before;other_state.position.x+=3;
        VehicleTrackSession other(model,course);
        check(other.start(other_state),"zero continuation other actor");
        const auto pa=race_contact_pose(before),pb=race_contact_pose(other.state());
        ContactRecord record;std::uint32_t seed=12345;RaceContactContext context{record,seed,4000};
        RacePairContact hit;
        check(resumed.meet(other,box,box,hit,&pa,&pb,&context)&&hit.searched&&hit.remaining_a==0,
            "zero-time post-impact completion");
        const auto& after=resumed.state();
        check(after.step==before.step&&after.simulation_seconds==before.simulation_seconds,
            "zero continuation retains the world clock");
        check(!after.rigid_body.linear_damping&&!after.rigid_body.angular_damping&&
            !after.rigid_body.alternate_yaw&&
            !(ai_driver?resumed.ai_physics().aero.enabled:after.aero.enabled),
            "original state1 disables damping alternate inertia and aero");
    }
    {
        auto spec=model.spec();spec.handling.centre_of_mass={0,0,0};
        RaceContactBox cube{{-1,-1,-1},{1,1,1},{}},rectangle{{-2,-1,-1},{2,1,1},{}};
        VehicleState first,second;second.position={8,0,0};
        RaceHullCache cache;RaceHullQuery q,previous;
        check(query_race_hulls(first,spec,cube,second,spec,rectangle,q,&cache)&&q.distance==5,
            "original asymmetric cached query");previous=q;
        second.rigid_body_ready=true;second.rigid_body.axes={{{0,1,0},{-1,0,0},{0,0,1}}};
        check(query_race_hulls(first,spec,cube,second,spec,rectangle,q,&cache)&&
            q.distance==5&&q.witness_a==previous.witness_a&&q.witness_b==previous.witness_b,
            "original clean rotation retains cached witnesses");
        cache.cached=false;
        check(query_race_hulls(first,spec,cube,second,spec,rectangle,q,&cache)&&q.distance==6,
            "original explicit dirty flag recomputes rotation");
        second.position.x=9;
        check(query_race_hulls(first,spec,cube,second,spec,rectangle,q,&cache)&&q.distance==7,
            "original translation invalidates query cache");
    }

    {
        auto spec=model.spec();
        spec.handling.centre_of_mass={0,0,0};
        const RaceContactBox cube{{-1,-1,-1},{1,1,1},{}};
        VehicleState a,b;
        b.position.x=2.2F;
        const auto first_a=race_contact_pose(a),first_b=race_contact_pose(b);
        RaceContactJob job;
        RaceHullCache cache;
        check(search_race_contact(a,spec,cube,b,spec,cube,first_a,first_b,job,&cache)
              && !job.admitted && cache.initialized && cache.cached,
              "registered pair retains closest features before contact");
        b.position.x=2.02F;
        a.simulation_seconds=b.simulation_seconds=.016F;
        check(search_race_contact(a,spec,cube,b,spec,cube,first_a,first_b,job,&cache)
              && job.admitted,"approaching pair uses its retained features on the next world step");
    }
    {
        // 5EBCE3 on the placed Stadium sign: translation is paired with Z.
        VehicleState sign;sign.rigid_body_ready=true;
        sign.position={1863.4442138671875F,-470.46685791015625F,69.03375244140625F};
        sign.rigid_body.axes={{{.8880615234375F,-.4591064453125F,.025390625F},
            {.459228515625F,.8883056640625F,-.0010986328125F},
            {-.0220947265625F,.0125732421875F,.9996337890625F}}};
        auto spec=model.spec();spec.handling.centre_of_mass={0,0,0};
        RaceContactBox hull;
        hull.points.push_back({-.2889976501464844F,-.06391529738903046F,-1.881485939025879F});
        ContactVector point{};unsigned index=0;
        check(race_hull_support(sign,spec,hull,{1,0,0},0,index,point)&&
              point==ContactVector{1863.1998291015625F,-470.41461181640625F,67.14568328857422F},
              "packed world transform preserves the native sign vertex");
    }
    #include "car_contact_search_pins.inc"
    #include "car_contact_flow_pins.inc"
    #include "car_contact_sample_pins.inc"

    {
        VehicleState state;state.published_support=4;
        VehicleTrackSession counted(model,course);
        check(counted.start(state),"contact count publication fixture");
        counted.publish_contact_count(2);
        check(counted.state().collision_body_flag==2&&counted.state().collision_state==0,
              "record publication leaves collision state for the outer physics call");
        check(counted.step(input,telemetry)&&counted.state().collision_state==2,
              "outer physics enters state2 with an active record");
        counted.publish_contact_count(0);
        check(counted.state().collision_body_flag==0&&counted.state().collision_state==2,
              "record retirement does not advance the outer physics state");
    }
    {VehicleState first;auto spec=model.spec();spec.handling.centre_of_mass={.25F,0,.28F};
#include "hull_support_native.inc"
    }
    // Complete original472950 queries, including penetration and large world
    // coordinates. The old affine minimizer changed overlapping witnesses.
    static const float query_pins[][31]={
#include "hull_query_native.inc"
    };
    for(const auto& pin:query_pins) {
        auto spec=model.spec();spec.handling.centre_of_mass={.25F,0,.28F};
        VehicleState first,second;unsigned at=0;
        for(auto* s:{&first,&second}) {
            s->position={pin[at],pin[at+1],pin[at+2]};at+=3;
            for(auto& axis:s->rigid_body.axes)for(float& x:axis)x=pin[at++];
            s->rigid_body_ready=true;s->rigid_orientation=s->orientation;
        }
        RaceHullQuery q;RaceHullCache cache;
        bool same=query_race_hulls(first,spec,box,second,spec,box,q,&cache)&&q.distance==pin[24];
        for(unsigned k=0;k<3;++k)same=same&&q.witness_a[k]==pin[25+k]&&q.witness_b[k]==pin[28+k];
        check(same,"original complete hull query with exact contact witnesses");
    }
    // Static shape and retained-query regression: resetting the support hint
    // to vertex0 selects a different contact arm on the shallow floor case.
    static const float polygon_pins[][43]={
#include "hull_polygon_native.inc"
    };
    unsigned polygon_case=0;
    const bool plane_admission[]={false,true,true,true,false,true,true,true,true};
    for(const auto& pin:polygon_pins) {
        auto spec=model.spec();spec.handling.centre_of_mass={.25F,0,.28F};
        VehicleState s;s.position={pin[0],pin[1],pin[2]};
        std::array<ContactVector,4> corners;ContactVector normal;
        for(unsigned i=0;i<4;++i)for(unsigned k=0;k<3;++k)corners[i][k]=pin[3+3*i+k];
        for(unsigned k=0;k<3;++k)normal[k]=pin[15+k];
        const auto bounds=race_track_body_bounds(spec,box);
        check(bounds.below==.24202686548233032F&&bounds.above==.8071292042732239F&&
            bounds.radius==2.375476598739624F&&
            bounds.centre==ContactVector{-.0934305191040039F,-.0034569501876831055F,.2825511693954468F},
            "original468A50 cylinder and hull centre");
        check(race_track_plane_admitted(s,bounds,corners,normal)==plane_admission[polygon_case++],
            "original469A00 polygon plane admission");
        RaceHullCache cache;RaceHullQuery query;
        for(unsigned pass=0;pass<3;++pass) {
            cache.cached=false;
            bool same=query_race_track_polygon(s,spec,box,corners,normal,pass==1,query,&cache);
            const unsigned at=18+(pass==1?7:0);
            same=same&&query.distance==pin[at];
            for(unsigned k=0;k<3;++k)same=same&&query.witness_a[k]==pin[at+1+k]&&query.witness_b[k]==pin[at+4+k];
            check(same,"original static shape witnesses including expansion invalidation");
        }
        s.simulation_seconds=double(.016F);auto history=race_contact_pose(s);history.time=0;
        history.position={pin[0]+normal[0]*.5F,pin[1]+normal[1]*.5F,pin[2]+normal[2]*.5F};
        RaceTrackContactJob job;
        bool same=search_race_track_contact(s,spec,box,history,corners,normal,
            {pin[0],pin[1],pin[2]},job,&cache)&&job.admitted==(pin[18]<=.0001F);
        if(job.admitted) {
            same=same&&job.remaining==pin[32]&&job.query.distance==pin[33];
            for(unsigned k=0;k<3;++k)same=same&&job.query.witness_a[k]==pin[34+k]&&job.query.witness_b[k]==pin[37+k];
            same=same&&job.correction.x==pin[40]&&job.correction.y==pin[41]&&job.correction.z==pin[42];
        }
        check(same,"original static contact history search and retained support hints");
    }
    static const float impact_pins[][12]={
#include "hull_impact_native.inc"
    };
    for(const auto& pin:impact_pins) {
        auto spec=model.spec();spec.handling.centre_of_mass={.25F,0,.28F};
        spec.handling.mass=1.22F;
        spec.handling.inertia={1.F/2.1760244369506836F,1.F/.44518163800239563F,1.F/.4223935008049011F};
        VehicleState first,second;first.alternate_yaw_active=second.alternate_yaw_active=false;
        second.position={pin[0],pin[1],pin[2]};
        first.velocity={pin[3],pin[4],pin[5]};second.velocity={pin[6],pin[7],pin[8]};
        const auto pa=race_contact_pose(first),pb=race_contact_pose(second);
        ContactRecord record;std::uint32_t seed=12345;RaceContactContext context{record,seed,1084227584};
        RacePairContact contact;
        check(resolve_swept_race_pair(first,spec,box,second,spec,box,contact,pa,pb,&context)&&
            contact.impulse==ContactVector{pin[9],pin[10],pin[11]},
            "original queued pair first impulse including separation and feature reduction");
    }
    // Original472950 separated retail CV witnesses; see contact_hull_native.py.
    {auto spec=model.spec();VehicleState first,second;RaceHullQuery query;
    spec.handling.centre_of_mass={0.25F,0.0F,0.2800000011920929F};second.position={4.38F,0.7F,0.0F};
    check(query_race_hulls(first,spec,box,second,spec,box,query),"original separated hull fixture");
    const ContactVector expected_a={2.085348129272461F,0.15620137751102448F,-0.10714227706193924F},expected_b={2.1174581050872803F,0.16264992952346802F,-0.10714173316955566F};
    check(std::abs(query.distance-0.03275108337402344F)<2e-6,"original separated hull distance");
    for(unsigned k=0;k<3;++k)check(std::abs(query.witness_a[k]-expected_a[k])<2e-6&&
        std::abs(query.witness_b[k]-expected_b[k])<2e-6,"original separated hull witnesses");}
    {auto spec=model.spec();VehicleState first,second;RaceHullQuery query;
    spec.handling.centre_of_mass={0.25F,0.0F,0.2800000011920929F};second.position={0.0F,2.0F,0.0F};
    check(query_race_hulls(first,spec,box,second,spec,box,query),"original separated hull fixture");
    const ContactVector expected_a={1.2963813543319702F,0.8738269805908203F,-0.19202686846256256F},expected_b={1.2963813543319702F,1.1192591190338135F,-0.19202686846256256F};
    check(std::abs(query.distance-0.24543213844299316F)<2e-6,"original separated hull distance");
    for(unsigned k=0;k<3;++k)check(std::abs(query.witness_a[k]-expected_a[k])<2e-6&&
        std::abs(query.witness_b[k]-expected_b[k])<2e-6,"original separated hull witnesses");}
    {auto spec=model.spec();VehicleState first,second;RaceHullQuery query;
    spec.handling.centre_of_mass={0.25F,0.0F,0.2800000011920929F};second.position={0.7F,2.0F,0.0F};
    check(query_race_hulls(first,spec,box,second,spec,box,query),"original separated hull fixture");
    const ContactVector expected_a={1.2963813543319702F,0.8738269805908203F,-0.19202686846256256F},expected_b={1.3011221885681152F,1.132025122642517F,-0.19202686846256256F};
    check(std::abs(query.distance-0.2582416832447052F)<2e-6,"original separated hull distance");
    for(unsigned k=0;k<3;++k)check(std::abs(query.witness_a[k]-expected_a[k])<2e-6&&
        std::abs(query.witness_b[k]-expected_b[k])<2e-6,"original separated hull witnesses");}
    {auto spec=model.spec();VehicleState first,second;RaceHullQuery query;
    spec.handling.centre_of_mass={0.25F,0.0F,0.2800000011920929F};second.position={0.0F,0.0F,2.0F};
    check(query_race_hulls(first,spec,box,second,spec,box,query),"original separated hull fixture");
    const ContactVector expected_a={0.07595989108085632F,-0.00029166979948058724F,0.7571291923522949F},expected_b={0.07595989108085632F,-0.00029166979948058724F,1.8079731464385986F};
    check(std::abs(query.distance-1.0508439540863037F)<2e-6,"original separated hull distance");
    for(unsigned k=0;k<3;++k)check(std::abs(query.witness_a[k]-expected_a[k])<2e-6&&
        std::abs(query.witness_b[k]-expected_b[k])<2e-6,"original separated hull witnesses");}

    VehicleState reset_a,reset_b;VehiclePose reset_pose=course.start;reset_pose.speed=12;
    check(model.reset(reset_a,reset_pose),"reset contact moving body");
    reset_pose.ground.x+=100;reset_pose.speed=0;
    check(model.reset(reset_b,reset_pose),"reset contact remote body");
    check(!reset_a.rigid_body_ready&&!reset_b.rigid_body_ready,"reset leaves rigid import pending");
    check(resolve_race_pair(reset_a,model.spec(),box,reset_b,model.spec(),box,hit)&&
        !hit.overlapping&&!reset_a.rigid_body_ready&&!reset_b.rigid_body_ready,
        "distant reset cannot abort pair pass or mutate pending bodies");
    reset_pose.ground.x=course.start.ground.x+4.2;
    check(model.reset(reset_b,reset_pose),"reset contact overlapping body");
    check(resolve_race_pair(reset_a,model.spec(),box,reset_b,model.spec(),box,hit)&&
        hit.applied&&reset_a.rigid_body_ready&&reset_b.rigid_body_ready&&
        reset_a.velocity.x<12&&reset_b.velocity.x>0,
        "real reset contact imports pose and exchanges momentum");

    // A fast closing pair must be resolved at first contact. The old end-pose
    // SAT chose the vertical minimum overlap and teleported a grounded car.
    for(bool side : {false,true}) {
        VehicleState moving,stopped;VehiclePose p=course.start;p.speed=100;
        check(model.reset(moving,p),"swept moving reset");
        auto q=p;q.speed=0;
        if(side)q.ground.y+=box.high.y-box.low.y+.01;
        else q.ground.x+=box.high.x-box.low.x+.01;
        check(model.reset(stopped,q),"swept target reset");
        // Isolate horizontal collision geometry from the authored stance pitch.
        moving.orientation={};stopped.orientation={};
        vehicle_import_rigid(model.spec().handling,moving);vehicle_import_rigid(model.spec().handling,stopped);
        const auto previous_a=race_contact_pose(moving),previous_b=race_contact_pose(stopped);
        moving.simulation_seconds+=.016F;stopped.simulation_seconds+=.016F;
        if(side){moving.position.y+=1.6;moving.velocity={0,100,0};}
        else moving.position.x+=1.6;
        vehicle_import_rigid(model.spec().handling,moving);
        auto legacy_a=moving,legacy_b=stopped;
        RacePairContact legacy;
        check(resolve_race_pair(legacy_a,model.spec(),box,legacy_b,model.spec(),box,legacy),"legacy deep pair probe");
        check(std::abs(legacy_a.position.z-moving.position.z)<1e-6,"deep hull separation does not choose the roof axis");
        const double height_a=moving.position.z,height_b=stopped.position.z;
        check(resolve_swept_race_pair(moving,model.spec(),box,stopped,model.spec(),box,hit,previous_a,previous_b)&&
            hit.searched&&hit.applied&&hit.contact_fraction>0&&hit.contact_fraction<1,"first-contact pair search");
        std::cout<<"SWEEP side="<<side<<" dz_a="<<moving.position.z-height_a<<" dz_b="<<stopped.position.z-height_b
            <<" depth="<<hit.depth<<" time="<<hit.contact_fraction<<"\n";
        check(std::abs(moving.position.z-height_a)<1e-6&&std::abs(stopped.position.z-height_b)<1e-6,
            "horizontal first contact does not reposition either car vertically");
        // Original46B260 can separate beyond the final time bracket; see native pins.
        check(moving.rigid_body.position[0]==float(moving.position.x)&&
            moving.rigid_body.position[1]==float(moving.position.y)&&
            moving.rigid_orientation.w==moving.orientation.w,"searched pose published to rigid component");
    }
    {
        // Repeated initial overlap must not combine old pose with newly
        // accumulated suspension velocity. Original469390/4692F0 restore the
        // linear velocity at the same contact epoch as the sampled pose.
        VehicleState first,second;VehiclePose p=course.start;p.speed=30;
        check(model.reset(first,p),"contact epoch moving reset");
        p.speed=0;p.ground.x+=4.2;
        check(model.reset(second,p),"contact epoch target reset");
        first.orientation={};second.orientation={};
        vehicle_import_rigid(model.spec().handling,first);
        vehicle_import_rigid(model.spec().handling,second);
        const auto start_a=race_contact_pose(first),start_b=race_contact_pose(second);
        first.velocity.z=15;
        vehicle_import_rigid(model.spec().handling,first);
        VehicleState sample;
        check(sample_race_contact(sample,model.spec(),start_a,first,0)&&
            sample.velocity.z==first.velocity.z,"zero-duration original sample retains current velocity");
        first.simulation_seconds=.016F;second.simulation_seconds=.016F;
        RaceContactJob job;
        check(search_race_contact(first,model.spec(),box,second,model.spec(),box,start_a,start_b,job)&&
            job.admitted&&job.queries==8&&job.remaining==.015937499701976776F,
            "initial penetration exhausts original eight midpoint queries");
        check(sample_race_contact(sample,model.spec(),start_a,first,job.remaining)&&
            std::abs(sample.velocity.z-(start_a.velocity.z+(15-start_a.velocity.z)*float((double(.016F)-job.remaining)/.016F)))<2e-6,
            "contact sample uses early history velocity instead of final suspension velocity");
    }

    {
        VehiclePose p=course.start;VehicleState falling,below;
        check(model.reset(below,p),"vertical collision lower car");
        p.ground.z+=box.high.z-box.low.z+.01;
        check(model.reset(falling,p),"vertical collision upper car");
        const auto upper=race_contact_pose(falling),lower=race_contact_pose(below);
        falling.simulation_seconds+=.016F;below.simulation_seconds+=.016F;
        falling.position.z-=.5;falling.velocity={0,0,-10};
        check(resolve_swept_race_pair(falling,model.spec(),box,below,model.spec(),box,hit,upper,lower)&&
            hit.searched&&hit.applied&&falling.velocity.z>-10&&below.velocity.z<0,
            "time search retains real vertical collision response");
    }

    {
        VehicleState first,second;VehiclePose p=course.start;
        check(model.reset(first,p),"separating first reset");p.ground.x+=4.2;p.speed=12;
        check(model.reset(second,p),"separating second reset");
        vehicle_import_rigid(model.spec().handling,first);vehicle_import_rigid(model.spec().handling,second);
        const auto pose_a=race_contact_pose(first),pose_b=race_contact_pose(second);
        RaceContactJob job;
        check(search_race_contact(first,model.spec(),box,second,model.spec(),box,pose_a,pose_b,job)&&job.admitted,
            "separating penetrating pair still receives original contact job");
        check(resolve_swept_race_pair(first,model.spec(),box,second,model.spec(),box,hit,pose_a,pose_b)&&!hit.applied&&hit.searched,
              "separating original job creates no current-hit impulse");
        check(std::abs(first.position.x-(pose_a.position.x-job.correction.x))<2e-6&&
              std::abs(second.position.x-(pose_b.position.x+job.correction.x))<2e-6,
              "nonclosing queued job retains original deep separation");
    }

    {
        // Three-body ordering: a separating overlap with the first neighbour
        // used to overwrite the middle car's history before its next pair.
        VehicleState moving,stopped;VehiclePose p=course.start;p.speed=30;
        check(model.reset(moving,p),"chain moving reset");
        p.speed=0;p.ground.x+=box.high.x-box.low.x+.1;
        check(model.reset(stopped,p),"chain stopped reset");
        moving.orientation={};stopped.orientation={};
        vehicle_import_rigid(model.spec().handling,moving);vehicle_import_rigid(model.spec().handling,stopped);
        auto previous=race_contact_pose(moving);const auto target=race_contact_pose(stopped);
        moving.simulation_seconds+=.016F;stopped.simulation_seconds+=.016F;
        moving.position.x+=1.6;moving.velocity.z=.1;
        vehicle_import_rigid(model.spec().handling,moving);
        RacePairContact no_impulse;no_impulse.overlapping=true;
        auto old_history=race_contact_pose(moving);
        auto old_a=moving,old_b=stopped;RacePairContact old_hit;
        check(resolve_swept_race_pair(old_a,model.spec(),box,old_b,model.spec(),box,
            old_hit,old_history,target),"chain legacy probe");
        update_race_contact_pose(previous,moving,no_impulse);
        const double height=moving.position.z;
        check(resolve_swept_race_pair(moving,model.spec(),box,stopped,model.spec(),box,
            hit,previous,target)&&hit.applied&&hit.contact_fraction>0,
            "chain retains first-contact motion after a non-impact overlap");
        std::cout<<"CHAIN old_dz="<<old_a.position.z-height<<" new_dz="<<moving.position.z-height<<"\n";
        check(std::abs(old_a.position.z-height)<1e-6&&std::abs(moving.position.z-height)<1e-6,
            "both initial-overlap and retained-history hull separation avoid vertical teleport");
    }

    // Reproduced in the traffic completion run: an impact sent a rival wheel
    // beyond the contact index. A valid empty index region is not a fatal
    // malformed query; let the existing bounds/reset lifecycle run.
    VehicleTrackSession outside(model,course);auto outside_pose=course.start;
    outside_pose.ground={500,500,0};
    check(outside.configure_ai(1)&&outside.start(outside_pose),"outside AI pose admitted");
    check(outside.step_ai(input,telemetry)&&
          telemetry.wheel_surface[2]==VehicleTrackSurface::outside&&
          telemetry.pending==VehicleTrackReset::none,
          "outside wheel misses continue until scheduled bounds check");
    bool bounds_ok=true;
    for(unsigned tick=2;tick<=kVehicleTrackBoundsPeriod-kVehicleTrackBoundsPhase;++tick)
        if(!outside.step_ai(input,telemetry)){bounds_ok=false;break;}
    check(bounds_ok&&telemetry.pending==VehicleTrackReset::out_of_bounds,
          "scheduled bounds check publishes pending recovery");
    check(outside.step_ai(input,telemetry)&&telemetry.reset==VehicleTrackReset::out_of_bounds,
          "outside body resets through existing tick owner");
    check(outside.step_ai(input,telemetry)&&outside.state().rigid_body_ready,
          "recovered rival continues with fresh contact state");

    VehicleTrackSession wall(model,course);
    auto pose=course.start;pose.ground={48,92,course.start.ground.z};
    pose.heading=vehicle_track_heading(16384);pose.speed=12;
    check(wall.configure_ai(1)&&wall.start(pose),"wall approach starts");
    bool contact=false,okay=true;
    for(unsigned n=0;n<40;++n) {
        if(!wall.step_ai(input,telemetry)){okay=false;break;}
        contact=contact||telemetry.contacts!=0;
    }
    check(okay&&contact,"AI hull contacts real proving-ground wall");
    check(std::isfinite(wall.state().position.z)&&wall.state().position.y<97,
          "AI remains on near side of wall");
    VehicleTrackSession traffic(model,course);VehicleState impacted;
    check(model.reset(impacted,pose)&&traffic.configure_ai(1),"traffic impact setup");
    auto stopped=input;stopped.throttle=0;stopped.brake=1;
    bool traffic_contact=false,traffic_ok=true;
    for(unsigned n=0;n<180;++n) {
        if(!traffic.step_ai_body(impacted,stopped,telemetry)){traffic_ok=false;break;}
        traffic_contact=traffic_contact||telemetry.contacts!=0;
        if(telemetry.reset!=VehicleTrackReset::none||telemetry.pending!=VehicleTrackReset::none)traffic_ok=false;
    }
    check(traffic_ok&&traffic_contact&&impacted.position.y<97&&std::isfinite(impacted.position.z),
          "impacted traffic keeps wall collision without racer resets");
    check(traffic.ai_physics().slices==360,"traffic retains selector3 state across frames");
    impacted.velocity.x+=3;impacted.rigid_body_ready=false;
    check(traffic.step_ai_body(impacted,stopped,telemetry)&&impacted.velocity.x>1,
          "traffic imports a later external pair impulse");
    auto published=impacted;
    published.velocity={20,1,0};published.rigid_body.axes={{{{1,0,0}},{{0,1,0}},{{0,0,1}}}};
    vehicle_publish_rigid(published);published.velocity={20,1,0};
    impacted.velocity={3,2,1};impacted.rigid_body.velocity={{3,2,1}};
    for(unsigned k=0;k<3;++k)impacted.rigid_body.momentum[k]=impacted.rigid_body.velocity[k]*model.spec().handling.mass;
    impacted.rigid_body.axes={{{{.98F,.12F,0}},{{-.12F,.98F,0}},{{0,0,1}}}};
    vehicle_publish_rigid(impacted);
    check(traffic.step_ai_body(impacted,stopped,telemetry,0,&published),"traffic resumes sampled rigid body");
    check(traffic.ai_physics().collision_context.actor_axes==race_contact_axes(published)&&
          traffic.ai_physics().collision_context.actor_velocity==PlayerRigidVector{{20,1,0}},
          "469390 preserves endpoint actor inputs until474730 after continuation");
    check(traffic.start(course.start)&&traffic.ai_physics().slices==0,"recycling clears old impact component");
    std::cout<<"AI track tests: "<<failures<<" failures\n";
    return failures?1:0;
}
