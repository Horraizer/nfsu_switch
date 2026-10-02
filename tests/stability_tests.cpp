#include "nfsu/race_session.h"
#include "nfsu/arg_path.h"
#include "nfsu/sha256.h"
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
void kernel() {
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
    VehicleModel model(stock_240sx_vehicle_spec());VehicleState a,b;VehiclePose pose;
    pose.ground.z=20;pose.speed=12;
    require(model.reset(a,pose),"airborne reset");b=a;
    a.angular_velocity={1,-2,3};a.velocity.z=b.velocity.z=8;
    const VehicleFlatGround floor;
    const double energy=dot(a.angular_velocity,a.angular_velocity);
    double minimum_height=a.position.z;
    for(int k=0;k<120;++k) {
        VehicleTelemetry x,y;
        require(model.step(a,{},floor,x)&&model.step(b,{},floor,y),"airborne integration");
        require(x.grounded_wheels==0 && y.grounded_wheels==0,"legitimate airborne state");
        require(a.position.x==b.position.x && a.position.y==b.position.y && a.position.z==b.position.z &&
                a.velocity.x==b.velocity.x && a.velocity.y==b.velocity.y && a.velocity.z==b.velocity.z,
                "angular contribution cannot change the jump trajectory");
        minimum_height=std::min(minimum_height,a.position.z);
    }
    require(minimum_height>=20 && a.position.z>23 && dot(a.angular_velocity,a.angular_velocity)<energy*.02,
            "jump survives while angular spin decays");
    std::cout<<"PASS native torque, gates, curve and independent airborne trajectory\n";
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
            require(a.step(c.script.input_at(k),x)&&b.step(c.script.input_at(k),y),"controlled scenario step");
            std::array<std::uint8_t,kVehicleTrackTelemetryRecordBytes> xb{},yb{};
            encode_vehicle_track_telemetry(x,xb);encode_vehicle_track_telemetry(y,yb);
            require(xb==yb,"controlled output deterministic");
            if(x.model.grounded_wheels==0)airborne=true;
            if(airborne&&x.model.grounded_wheels>=2)landed=true;
            if(x.model.grounded_wheels>=2)require(dot(x.model.low_support_torque,x.model.low_support_torque)==0,"planted damping disabled");
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
        course.start.speed=speed;
        RaceSession a(course,stock_240sx_vehicle_spec(),RaceSteering(mode));
        RaceSession b(course,stock_240sx_vehicle_spec(),RaceSteering(mode));
        require(a.start(error)&&b.start(error),"flat starts");
        const std::string label="flat-"+std::to_string(int(speed))+"-"+std::to_string(axis);
        for(int tick=0;tick<600;++tick) {
            const auto pad=race_input_frame({1,tick>0?gamepad_button_bit(GamepadButton::trigger_right):0,
                tick>=60&&tick<480?axis:0},tick);
            require(a.advance(pad,error)&&b.advance(pad,error),"flat advances");
            require(a.record()==b.record(),"flat deterministic");
            for(const auto& x:a.physics_telemetry()) {
                if(!x.step)continue;
                require(x.contacts==0 && x.resets==0,"no contact cornering stays clear");
                const auto& q=x.orientation;
                require(1-2*(q.x*q.x+q.y*q.y)>.98,"flat cornering stays upright");
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

void retail(const std::filesystem::path& root,std::ostream* output) {
    VehicleTrackCourse course;std::string error;
    require(load_vehicle_track_course(root/"Tracks/TRACKB1003.lzc",2,0,0,0,course,error),"retail course");
    for(int mode=0;mode<3;++mode) {
        RaceSession a(course,stock_240sx_vehicle_spec(),RaceSteering(mode)),b(course,stock_240sx_vehicle_spec(),RaceSteering(mode));
        require(a.start(error)&&b.start(error),"race starts");
        unsigned rng=7;int axis=0;double minup=1;bool saw_damping=false;
        for(int tick=0;tick<720;++tick) {
            if(tick%120==0){rng=rng*1664525U+1013904223U;axis=(int((rng>>16)%7)-3)*10000;}
            auto pad=race_input_frame({1,tick<163?0:gamepad_button_bit(GamepadButton::trigger_right),tick<400?0:axis},tick);
            require(a.advance(pad,error)&&b.advance(pad,error),"seed7 race advances");
            require(a.record()==b.record(),"complete race deterministic");
            for(const auto& t:a.physics_telemetry()) {
                if(!t.step)continue;
                const auto& q=t.orientation;
                minup=std::min(minup,1-2*(q.x*q.x+q.y*q.y));
                saw_damping=saw_damping||dot(t.model.low_support_torque,t.model.low_support_torque)>0;
                require(t.resets==0,"no reset conceals the rollover");
                if(output)write(*output,"event1-seed7",mode,t);
            }
        }
        require(minup>.9 && saw_damping,"seed7 low-support rollover regression");
        auto pad=race_input_frame({1,gamepad_button_bit(GamepadButton::select),0},720);
        require(a.advance(pad,error)&&a.telemetry().reset==VehicleTrackReset::manual,"manual reset remains available");
        std::cout<<"PASS seed7 mode "<<mode<<" min-up "<<std::setprecision(17)<<minup<<" no automatic reset\n";
    }
}
}
int main(int argc,char** argv) {
    try {
        std::string error;require(check_arguments(argc,argv,1,error),"arguments");
        std::filesystem::path root;std::ofstream output;
        for(int i=1;i<argc;++i) {
            const std::string option=argv[i];require(i+1<argc,"option argument");
            if(option=="--root")root=argument_path(argv[++i]);
            else if(option=="--out") {const auto path=argument_path(argv[++i]);require(!std::filesystem::exists(path),"output must be fresh");output.open(path);require(bool(output),"output open");output<<std::setprecision(17);}
            else require(false,"unknown option");
        }
        kernel();proving(output.is_open()?&output:nullptr);no_contact(output.is_open()?&output:nullptr);curbs(output.is_open()?&output:nullptr);
        if(!root.empty())retail(root,output.is_open()?&output:nullptr);
        if(output.is_open()){output.flush();require(bool(output),"output write");}
        std::cout<<"stability tests: all checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
