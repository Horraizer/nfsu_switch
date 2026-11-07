#include "nfsu/race_session.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/arg_path.h"
#include "nfsu/sha256.h"
#include "vehicle_runtime_oracles.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace nfsu;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
double dot(const VehicleVec3& a,const VehicleVec3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
struct Pin { VehicleVec3 omega; double mass; unsigned wheels,state; VehicleVec3 torque; };
const Pin pins[] = {
#include "low_support_native.inc"
};
void jump_applicability() {
    const auto h=stock_240sx_handling();
    PlayerRigidBodyState body;
    body.position={{2,3,4}};body.momentum={{14,2,8}};body.velocity={{12,1,8}};
    body.angular_momentum={{4,5,6}};body.angular_velocity={{1,-2,3}};
    body.force={{2,3,-11.956F}};body.torque={{1,2,3}};
    body.snapshot_axes[0]={{0,1,0}};body.adaptive_damping=.05F;body.yaw_torque_limit=7;
    body.linear_damping=false;body.alternate_yaw=true;body.gravity=false;
    const auto before=body;
    const auto applied=player_low_support_apply(h,body,0,0);
    require(applied!=PlayerRigidVector{} && body.torque!=before.torque,"nonzero angular damping exercised");
    for(unsigned k=0;k<3;++k)
        require(body.torque[k]==before.torque[k]+applied[k],"original torque addition");
    require(body.position==before.position && body.momentum==before.momentum &&
            body.angular_momentum==before.angular_momentum && body.velocity==before.velocity &&
            body.angular_velocity==before.angular_velocity && body.force==before.force &&
            body.axes==before.axes && body.snapshot_axes==before.snapshot_axes &&
            body.adaptive_damping==before.adaptive_damping && body.yaw_torque_limit==before.yaw_torque_limit &&
            body.linear_damping==before.linear_damping && body.angular_damping==before.angular_damping &&
            body.alternate_yaw==before.alternate_yaw && body.gravity==before.gravity,
            "fixed-input low-support operation writes torque alone");

    // Held actual jump step1 snapshots. Authenticated speed.exe/stock240SX:
    // full462F90/584CD0/5ABDB5,007F, authentic476E97 cache1/1/0/0.
    // The force pins belong to these two operations, not a whole trajectory.
    const PlayerRigidMatrix axes[] = {
        {{{{1.0001195669174194F,.025085769593715668F,.0062097087502479553F}},
          {{-.025000004097819328F,1,.0083333328366279602F}},
          {{-.0062097087502479553F,-.0080714765936136246F,1.0001195669174194F}}}},
        {{{{.99994534254074097F,0,-.01045604795217514F}},{{0,1,0}},
          {{.01045604795217514F,0,.99994534254074097F}}}}
    };
    const PlayerRigidVector original_force[] = {
        {{-.12738347053527832F,.013702111318707466F,-13.744690895080566F}},
        {{-.15489648282527924F,0,-13.669923782348633F}}
    };
    std::array<PlayerRigidVector,2> force{};
    for(unsigned k=0;k<2;++k) {
        PlayerRigidBodyState held;
        held.axes=held.snapshot_axes=axes[k];
        held.velocity={{11.998284339904785F,0,7.9067368507385254F}};
        held.force={{0,0,-11.956000328063965F}};
        player_aero_apply(h,held,PlayerAeroCache{},PlayerInitialAeroConfiguration{});
        require(held.force==original_force[k],"original orientation-dependent aero force witness");
        force[k]=held.force;
    }
    require(force[0]!=force[1],"equal velocity and different axes change original aero force");

    // Conditional high-speed control only: common adaptive damping can couple
    // angular and linear motion elsewhere even when aero is disabled.
    auto spec=stock_240sx_vehicle_spec();spec.aero.enabled=false;
    VehicleModel model(spec);VehicleState a,b;VehiclePose pose;pose.ground.z=20;pose.speed=12;
    require(model.reset(a,pose),"conditional airborne reset");b=a;
    a.angular_velocity={1,-2,3};a.velocity.z=b.velocity.z=8;
    for(unsigned k=0;k<120;++k) {
        VehicleTelemetry x,y;
        require(model.step(a,{},VehicleFlatGround{},x)&&model.step(b,{},VehicleFlatGround{},y),
                "conditional airborne integration");
        require(x.grounded_wheels==0 && y.grounded_wheels==0,"conditional legitimate airborne state");
        require(a.position.x==b.position.x && a.position.y==b.position.y && a.position.z==b.position.z &&
                a.velocity.x==b.velocity.x && a.velocity.y==b.velocity.y && a.velocity.z==b.velocity.z,
                "same high-speed jump has equal translation with aero disabled");
    }
    std::cout<<"PASS torque-only write domain, original aero force counterfactual and conditional jump\n";
}
struct AngularPin { unsigned step;PlayerRigidVector omega,momentum; };
const AngularPin angular_pins[] = {
#include "angular_response_native.inc"
};
struct FlatRollNative {std::array<float,42> input,raw;std::array<float,9> normalized;};
const FlatRollNative flat_roll_pins[]={
#include "flat_roll_native.inc"
};
void flat_roll_kernel_witness() {
    unsigned below_old_bound=0;
    for(const auto& pin:flat_roll_pins) {
        const auto& row=pin.input;auto h=stock_240sx_handling();
        h.mass=row[0];for(unsigned i=0;i<3;++i)h.inertia[i]=row[1+i];h.alternate_yaw_scale=row[4];
        PlayerRigidBodyState body;
        const auto get=[&](unsigned i){return PlayerRigidVector{{row[i],row[i+1],row[i+2]}};};
        body.position=get(6);body.momentum=get(9);body.angular_momentum=get(12);
        for(unsigned i=0;i<3;++i)body.axes[i]=get(15+i*3);
        body.velocity=get(24);body.angular_velocity=get(27);body.force=get(30);body.torque=get(33);
        body.adaptive_damping=row[36];body.yaw_torque_limit=row[37];body.alternate_yaw=row[38]!=0;
        body.angular_damping=row[39]!=0;body.linear_damping=row[40]!=0;body.gravity=row[41]!=0;
        player_rigid_body_integrate(h,body,row[5]);
        auto actual=row;unsigned at=6;
        for(const auto* v:{&body.position,&body.momentum,&body.angular_momentum})for(float x:*v)actual[at++]=x;
        for(const auto& v:body.axes)for(float x:v)actual[at++]=x;
        for(const auto* v:{&body.velocity,&body.angular_velocity,&body.force,&body.torque})for(float x:*v)actual[at++]=x;
        actual[at++]=body.adaptive_damping;
        require(at==37&&std::memcmp(actual.data(),pin.raw.data(),sizeof(actual))==0,"exact original body-only raw update in supplied flat-turn state");
        player_rigid_body_normalize(body);at=0;std::array<float,9> normalized{};
        for(const auto& v:body.axes)for(float x:v)normalized[at++]=x;
        require(std::memcmp(normalized.data(),pin.normalized.data(),sizeof(normalized))==0,"exact original orientation normalization in supplied flat-turn state");
        below_old_bound+=body.axes[2][2]<.98F?1U:0U;
    }
    require(below_old_bound==2,"original body kernel itself crosses the old unconditional up bound");
    std::cout<<"PASS four original supplied flat-turn body updates and normalization; no whole-player parity claim"<<std::endl;
}
void kernel() {
    flat_roll_kernel_witness();
    std::vector<unsigned char> bytes;
    for (const auto& p:vehicle_low_support_curve()) for (float v:p) {
        std::uint32_t bits=0;std::memcpy(&bits,&v,4);
        for(unsigned k=0;k<4;++k)bytes.push_back(static_cast<unsigned char>(bits>>(8*k)));
    }
    require(sha256_hex(sha256(bytes.data(),bytes.size()))==
        "62FD529EABD338B8EB939B55719645103273056561739D7299E323E05D79E8DE","native curve bytes");
    for(const auto& p:pins) {
        const auto t=vehicle_low_support_torque(p.omega,p.mass,p.wheels,p.state);
        require(std::fabs(t.x-p.torque.x)<.05 && std::fabs(t.y-p.torque.y)<.05 &&
                std::fabs(t.z-p.torque.z)<.05,"native torque pin");
        require(dot(t,p.omega)<=0,"damping dissipates angular energy");
    }
    jump_applicability();
    VehicleModel model(stock_240sx_vehicle_spec());VehicleState a,b;VehiclePose pose;
    pose.ground.z=20;pose.speed=12;
    require(model.reset(a,pose),"airborne reset");b=a;
    a.angular_velocity={1,-2,3};a.velocity.z=b.velocity.z=8;
    const VehicleFlatGround floor;
    unsigned matched=0;
    double minimum_height=a.position.z;
    for(int k=0;k<120;++k) {
        VehicleTelemetry x,y;
        require(model.step(a,{},floor,x)&&model.step(b,{},floor,y),"airborne integration");
        require(x.grounded_wheels==0 && y.grounded_wheels==0,"legitimate airborne state");
        minimum_height=std::min(minimum_height,a.position.z);
        for(const auto& pin:angular_pins)if(pin.step==unsigned(k+1)) {
            ++matched;
            require(std::memcmp(a.rigid_body.angular_velocity.data(),pin.omega.data(),3*sizeof(float))==0&&
                    std::memcmp(a.rigid_body.angular_momentum.data(),pin.momentum.data(),3*sizeof(float))==0,
                    "original coupled angular velocity AND momentum in declared jump fixture");
        }
    }
    require(matched==6,"all original angular response checkpoints reached");
    require(minimum_height>=20 && a.position.z>23,"jump survives above existing height bounds");
    std::cout<<"PASS native torque, gates, curve and airborne survival\n";
}
void write(std::ostream& out,const char* label,int mode,const VehicleTrackTelemetry& t) {
    const auto vec=[&](const VehicleVec3& v){out<<','<<v.x<<','<<v.y<<','<<v.z;};
    out<<label<<','<<mode<<','<<t.step;vec(t.position);
    out<<','<<t.orientation.w<<','<<t.orientation.x<<','<<t.orientation.y<<','<<t.orientation.z;
    vec(t.model.velocity);vec(t.velocity);vec(t.model.angular_velocity);vec(t.angular_velocity);
    out<<','<<t.model.requested_steer_angle<<','<<t.model.steer_angle<<','<<t.model.forward_speed<<','<<t.model.gear
       <<','<<t.model.grounded_wheels;vec(t.model.low_support_torque);
    out<<','<<t.contacts<<','<<t.impulses<<','<<unsigned(t.reset);
    for(const auto& w:t.model.wheels){out<<','<<w.contact<<','<<w.compression<<','<<w.load;vec(w.ground_point);vec(w.ground_normal);}
    out<<'\n';
    for(const auto& e:t.contact_events) {
        out<<"contact,"<<mode<<','<<t.step<<','<<e.order<<','<<e.polygon<<','<<e.hull_point<<','<<unsigned(e.surface)
           <<','<<e.depth<<','<<e.approach;vec(e.normal);vec(e.plane_point);vec(e.arm);vec(e.separation);
        vec(e.velocity_before);vec(e.angular_before);vec(e.velocity_after);vec(e.angular_after);
        vec(e.contact_impulse);vec(e.applied_impulse);vec(e.accumulated_impulse);out<<'\n';
    }
}
void proving(std::ostream* output) {
    VehicleTrackCourse course;std::string error;
    require(build_vehicle_proving_ground(course,error),"proving ground");
    VehicleModel model(stock_240sx_vehicle_spec());
    for(const auto& c:vehicle_track_scenarios(VehicleTrackCourseKind::proving_ground)) {
        VehicleTrackSession a(model,course),b(model,course);
        const auto pose=vehicle_track_scenario_pose(course.start,c);
        require(a.start(pose)&&b.start(pose),"controlled scenario start");
        bool airborne=false, landed=false;
        for(std::uint64_t k=0;k<c.script.total_steps();++k) {
            VehicleTrackTelemetry x,y;
            candidate_oracles::support.clear();candidate_oracles::capture_support=true;
            require(a.step(c.script.input_at(k),x)&&b.step(c.script.input_at(k),y),"controlled scenario step");
            candidate_oracles::capture_support=false;
            std::array<std::uint8_t,kVehicleTrackTelemetryRecordBytes> xb{},yb{};
            encode_vehicle_track_telemetry(x,xb);encode_vehicle_track_telemetry(y,yb);
            require(xb==yb,"controlled output deterministic");
            if(x.model.grounded_wheels==0)airborne=true;
            if(airborne&&x.model.grounded_wheels>=2)landed=true;
            //4791F0 consumes the outer-published support, not the later
            // contact count in end-step telemetry. Check the actual callback
            // epoch rather than treating a newly landed wheel as prior support.
            require(!candidate_oracles::support.empty(),"original support callback observed");
            for(const auto& event:candidate_oracles::support)
                if(event.published>=2||event.state==1||event.state==2)
                    require(event.applied==PlayerRigidVector{},"published planted/state gate disables damping");
            VehicleVec3 total;
            unsigned order=0;
            for(const auto& e:x.contact_events) {
                require(e.order==order++ && e.order<x.contacts && e.hull_point<kVehicle240sxHullPoints,"ordered diagnostic");
                const double norm=dot(e.normal,e.normal);
                require(std::fabs(norm-1)<1e-5,"valid contact normal");
                total.x+=e.applied_impulse.x;total.y+=e.applied_impulse.y;total.z+=e.applied_impulse.z;
                require(total.x==e.accumulated_impulse.x && total.y==e.accumulated_impulse.y &&
                        total.z==e.accumulated_impulse.z,"current hit distinct from accumulated impulse");
            }
            require(total.x==x.impulse.x&&total.y==x.impulse.y&&total.z==x.impulse.z,"contact sum matches existing solver");
            if(output)write(*output,c.name.c_str(),0,x);
        }
        if(c.name=="drop")require(airborne&&landed,"real drop lands without hidden reset");
    }
    std::cout<<"PASS deterministic walls, slope/crest, drop/landing, ground loss and reset matrix\n";
}


void no_contact(std::ostream* output) {
    VehicleTrackLayout layout;layout.name="stability-flat";
    for(int x=-512;x<512;x+=32)for(int y=-512;y<512;y+=32) {
        VehicleTrackQuad q;
        const float c[4][3]={{float(x),float(y),10},{float(x+32),float(y),10},
            {float(x+32),float(y+32),10},{float(x),float(y+32),10}};
        std::memcpy(q.corner,c,sizeof(c));layout.quads.push_back(q);
    }
    layout.loop={{{-400,-400}},{{400,-400}},{{400,400}},{{-400,400}}};
    layout.grid[2]=10;
    VehicleTrackCourse course;std::string error;
    require(build_vehicle_track_course(layout,course,error),"flat course");
    for(double speed:{0.,20.,35.,50.})for(int axis:{12000,32767})for(int mode=0;mode<3;++mode) {
        course.start.speed=0;
        RaceSession a(course,stock_240sx_vehicle_spec(),RaceSteering(mode));
        RaceSession b(course,stock_240sx_vehicle_spec(),RaceSteering(mode));
        require(a.start(error)&&b.start(error),"flat starts");
        const std::string label="flat-"+std::to_string(int(speed))+"-"+std::to_string(axis);
        for(int tick=0;tick<600;++tick) {
            // A moving diagnostic state is resumed after GO. Holding it on
            // a countdown grid would replace its road gear with neutral/first.
            if(tick==163&&speed>0) {
                auto moving=course.start;moving.speed=speed;
                VehicleModel model(stock_240sx_vehicle_spec());
                require(model.reset(const_cast<VehicleState&>(a.car()),moving)&&
                        model.reset(const_cast<VehicleState&>(b.car()),moving),"controlled rolling resume after GO");
            }

            const auto pad=race_input_frame({1,tick>0?gamepad_button_bit(GamepadButton::trigger_right):0,
                tick>=60&&tick<480?axis:0},tick);
            require(a.advance(pad,error)&&b.advance(pad,error),"flat advances");
            require(a.record()==b.record(),"flat deterministic");
            for(const auto& x:a.physics_telemetry()) {
                if(!x.step)continue;
                require(x.contacts==0 && x.resets==0,"no contact cornering stays clear");
                const auto& q=x.orientation;
                // Native body-only pins above disprove an unconditional .98 up
                // bound. This stress checks rollover/clearance; exact native
                // response is checked separately, not fitted to a roll angle.
                require(1-2*(q.x*q.x+q.y*q.y)>0,"flat cornering does not overturn");
                if(output)write(*output,label.c_str(),mode,x);
            }
        }
    }
    std::cout<<"PASS paired no-contact cornering speeds and axes in all steering modes\n";
}

void curbs(std::ostream* output) {
    const VehicleModel model(stock_240sx_vehicle_spec());
    for(float height:{.125F,.25F,.5F}) {
        VehicleTrackLayout layout;layout.name="stability-curb";
        for(int x=-64;x<96;x+=32)for(int y=-96;y<96;y+=32) {
            VehicleTrackQuad q;const float h=x<0?10:10+height;
            const float c[4][3]={{float(x),float(y),h},{float(x+32),float(y),h},
                {float(x+32),float(y+32),h},{float(x),float(y+32),h}};
            std::memcpy(q.corner,c,sizeof(c));layout.quads.push_back(q);
            if(x==0) {
                VehicleTrackQuad face;
                const float v[4][3]={{0,float(y),10},{0,float(y+32),10},
                    {0,float(y+32),10+height},{0,float(y),10+height}};
                std::memcpy(face.corner,v,sizeof(v));layout.quads.push_back(face);
            }
        }
        layout.grid[0]=-8;layout.grid[2]=10;
        VehicleTrackCourse course;std::string error;
        require(build_vehicle_track_course(layout,course,error),"curb course");
        for(double speed:{10.,20.,35.,50.})for(int angle:{0,2731,8192}) {
            VehicleTrackSession a(model,course),b(model,course);
            auto pose=course.start;pose.heading=vehicle_track_heading(angle);pose.speed=speed;
            require(a.start(pose)&&b.start(pose),"curb start");
            bool reached=false;
            const std::string label="curb-"+std::to_string(int(height*1000))+"-"+
                std::to_string(int(speed))+"-"+std::to_string(angle);
            for(int k=0;k<240;++k) {
                VehicleTrackTelemetry x,y;
                require(a.step({},x)&&b.step({},y),"curb step");
                std::array<std::uint8_t,kVehicleTrackTelemetryRecordBytes> xb{},yb{};
                encode_vehicle_track_telemetry(x,xb);encode_vehicle_track_telemetry(y,yb);
                require(xb==yb,"curb response deterministic");
                require(x.resets==0,"no reset conceals curb response");
                reached=reached||x.contacts>0;
                for(const auto& w:x.model.wheels)
                    reached=reached||(w.contact && std::fabs(w.ground_point.z-(10+height))<1e-6);
                if(output)write(*output,label.c_str(),0,x);
            }
            require(reached,"raised curb reached by wheel or body");
        }
    }
    std::cout<<"PASS paired curb heights, speeds and approach angles with body contacts\n";
}

// A controlled brief edge transition: the front-left wheel starts on the
// upper quadrant, the other wheels above the lower road. Advancing the real
// RaceSession after GO exercises one-wheel damping before the rear-left
// wheel reaches the upper road. Nonzero pad steering reaches every mode.
void support_activation(std::ostream* output) {
    VehicleTrackLayout layout;layout.name="stability-one-wheel";
    const auto quad=[&](float x0,float y0,float x1,float y1,float z) {
        VehicleTrackQuad q;
        const float c[4][3]={{x0,y0,z},{x1,y0,z},{x1,y1,z},{x0,y1,z}};
        std::memcpy(q.corner,c,sizeof(c));layout.quads.push_back(q);
    };
    // This is an edge, not two unmarked overlapping road decks.440A40 may
    // reuse a strict-interior cached polygon, unlike the legacy finite ray.
    // Tile the lower floor around the upper quadrant so both adapters see
    // the intended single upper wheel without inventing multilayer flags.
    quad(-32,-32,32,0,8);quad(-32,0,0,32,8);quad(0,0,32,32,10);
    layout.loop={{{-24,-24}},{{24,-24}},{{24,24}},{{-24,24}}};layout.grid[2]=10;
    VehicleTrackCourse course;std::string error;
    require(build_vehicle_track_course(layout,course,error),"one-wheel course");
    course.start.speed=20;
    for(int mode=0;mode<3;++mode) {
        RaceSession a(course,stock_240sx_vehicle_spec(),RaceSteering(mode));
        RaceSession b(course,stock_240sx_vehicle_spec(),RaceSteering(mode));
        require(a.start(error)&&b.start(error),"one-wheel sessions start");
        bool low=false,damping=false,steering=false,steered_damping=false;double minup=1;
        for(int tick=0;tick<174;++tick) {
            const auto pad=race_input_frame({1,tick<163?0:gamepad_button_bit(GamepadButton::trigger_right),
                tick<164?0:12000},tick);
            require(a.advance(pad,error)&&b.advance(pad,error),"one-wheel sessions advance");
            require(a.record()==b.record(),"one-wheel complete state deterministic");
            for(const auto& t:a.physics_telemetry()) {
                if(!t.step)continue;
                const auto q=t.orientation;
                minup=std::min(minup,1-2*(q.x*q.x+q.y*q.y));
                low=low||t.model.grounded_wheels<2;
                damping=damping||dot(t.model.low_support_torque,t.model.low_support_torque)>0;
                steering=steering||std::fabs(t.model.steer_angle)>0;
                steered_damping=steered_damping||(t.model.grounded_wheels<2 &&
                    dot(t.model.low_support_torque,t.model.low_support_torque)>0 &&
                    std::fabs(t.model.steer_angle)>0);
                require(t.resets==0 && t.contacts==0,"edge witness has no concealed reset or hull impulse");
                if(output)write(*output,"one-wheel-activation",mode,t);
            }
        }
        require(low&&damping&&steering&&steered_damping,"actual low-support damping and pad steering exercised in every mode");
        require(minup>.9,"one-wheel transition retains rollover safety");
        std::cout<<"PASS one-wheel mode "<<mode<<" min-up "<<std::setprecision(17)<<minup<<" damping activated\n";
    }
}

void retail(const std::filesystem::path& root,std::ostream* output) {
    VehicleTrackCourse course;std::string error;
    require(load_vehicle_track_course(root/"Tracks/TRACKB1003.lzc",2,0,0,0,course,error),"retail course");
    for(int mode=0;mode<3;++mode) {
        RaceSession a(course,stock_240sx_vehicle_spec(),RaceSteering(mode)),b(course,stock_240sx_vehicle_spec(),RaceSteering(mode));
        require(a.start(error)&&b.start(error),"race starts");
        unsigned rng=7;int axis=0;double minup=1;bool saw_damping=false,hit_body=false;unsigned recoveries=0,eligible_ticks=0;
        for(int tick=0;tick<720;++tick) {
            if(tick%120==0){rng=rng*1664525U+1013904223U;axis=(int((rng>>16)%7)-3)*10000;}
            auto pad=race_input_frame({1,tick<163?0:gamepad_button_bit(GamepadButton::trigger_right),tick<400?0:axis},tick);
            const auto& before=a.car();
            const double before_up=1-2*(before.orientation.x*before.orientation.x+before.orientation.y*before.orientation.y);
            const auto policy=vehicle_track_first_lap_approximations();
            const bool eligible=before_up<0&&vehicle_sqrt(dot(before.velocity,before.velocity))<policy.upside_down_speed;
            eligible_ticks=eligible?eligible_ticks+1:0;
            require(a.advance(pad,error)&&b.advance(pad,error),"seed7 race advances");
            require(a.record()==b.record(),"complete race deterministic");
            for(const auto& t:a.physics_telemetry()) {
                if(!t.step)continue;
                const auto& q=t.orientation;
                minup=std::min(minup,1-2*(q.x*q.x+q.y*q.y));
                saw_damping=saw_damping||dot(t.model.low_support_torque,t.model.low_support_torque)>0;
                hit_body=hit_body||t.contacts!=0;
                if(!hit_body)require(1-2*(q.x*q.x+q.y*q.y)>0,"unhit supported retail car does not overturn");
                require(t.reset!=VehicleTrackReset::stuck,"unconfigured stuck recovery cannot conceal a failure");
                if(t.reset==VehicleTrackReset::upside_down)require(hit_body&&eligible_ticks+1>=policy.upside_down_steps/kVehicleTrackStepsPerTick,"configured port rollover recovery needs a prior actual impact and its slow-inverted interval");
                if(t.reset!=VehicleTrackReset::none){++recoveries;hit_body=false;require(t.reset==VehicleTrackReset::upside_down||t.reset==VehicleTrackReset::zone||t.reset==VehicleTrackReset::zone_to_point||t.reset==VehicleTrackReset::off_path||t.reset==VehicleTrackReset::out_of_bounds||t.reset==VehicleTrackReset::sunk,"retail stress permits documented world/configured rollover recoveries");}
                if(output)write(*output,"event1-seed7",mode,t);
            }
        }
        std::cout<<"seed7 complete motion min-up "<<minup<<" configured recoveries "<<recoveries<<std::endl;
        // Activation belongs to the controlled edge above in every mode.
        // This arbitrary full-input route can hit barriers and overturn; the
        // configured port recovery is checked against its prior interval,
        // never described as recovered original player behavior.
        // Activation is required by the controlled one-wheel fixture in
        // every mode. This arbitrary route input need not leave the road.
        std::cout<<"seed7 observed low-support damping "<<saw_damping<<std::endl;
        auto pad=race_input_frame({1,gamepad_button_bit(GamepadButton::select),0},720);
        require(a.advance(pad,error)&&a.telemetry().reset==VehicleTrackReset::manual,"manual reset remains available");
        std::cout<<"PASS seed7 mode "<<mode<<" min-up "<<std::setprecision(17)<<minup<<" configured recovery only\n";
    }
}
}
int main(int argc,char** argv) {
    const bool applicability_only=argc==2 && std::string(argv[1])=="--applicability-only";
    try {
        std::string error;require(check_arguments(argc,argv,1,error),"arguments");
        if(argc==2&&std::string(argv[1])=="--kernel-only") {
            kernel();std::cout<<"SUMMARY kernel failures=0\n";return 0;
        }
        if(applicability_only) {
            jump_applicability();std::cout<<"SUMMARY applicability failures=0\n";return 0;
        }
        std::filesystem::path root;std::ofstream output;
        for(int i=1;i<argc;++i) {
            const std::string option=argv[i];require(i+1<argc,"option argument");
            if(option=="--root")root=argument_path(argv[++i]);
            else if(option=="--out") {const auto path=argument_path(argv[++i]);require(!std::filesystem::exists(path),"output must be fresh");output.open(path);require(bool(output),"output open");output<<std::setprecision(17);}
            else require(false,"unknown option");
        }
        kernel();support_activation(output.is_open()?&output:nullptr);proving(output.is_open()?&output:nullptr);no_contact(output.is_open()?&output:nullptr);curbs(output.is_open()?&output:nullptr);
        if(!root.empty())retail(root,output.is_open()?&output:nullptr);
        if(output.is_open()){output.flush();require(bool(output),"output write");}
        std::cout<<"stability tests: all checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';
        if(applicability_only)std::cerr<<"SUMMARY applicability failures=1\n";
        return 1;}
}
