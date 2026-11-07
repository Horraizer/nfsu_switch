#include "nfsu/race_session.h"
#include "nfsu/race_particles.h"
#include "nfsu/race_result.h"
#include <cmath>
#include <iostream>
using namespace nfsu;
namespace {
unsigned failures=0;
void check(bool b,const char* label){if(!b){std::cerr<<"FAIL: "<<label<<'\n';++failures;}}
struct World:CrashCameraWorld {
    bool ground(const AiDriverPoint&,float& h)const override{h=0;return true;}
};
struct Road:CrashCameraRoadSource {
    bool available=true;
    bool prepare(const VehicleState& c,float,AiRoadChain& chain,int& category)const override {
        if(!available)return false;
        chain={};chain.count=12;category=0;
        for(unsigned j=0;j<chain.count;++j){auto& p=chain.cells[j];p.distance=j*10.F;
            p.point.position[0]=float(c.position.x)-1.F+j*10.F;p.point.position[1]=float(c.position.y);
            p.point.height=float(c.position.z);p.point.forward[0]=1;p.point.forward[1]=0;p.point.unknown_24=p.point.unknown_28=10;p.lower[0]=-10;p.upper[0]=10;}
        return true;
    }
    bool progress_at(const AiDriverPoint&,float& p)const override{p=90;return true;}
};
}
namespace nfsu {
struct RaceWorldContactProbe {
    static void statistics_clock(RaceSession& s) {
        s.frame_seconds_=double(1.F/60.F*.4F);s.timing_.go_valid=true;s.timing_.go_step=0;
        const_cast<VehicleState&>(s.car()).velocity={400,0,0};
    }
    static void impact_placement(RaceSession& s) {
        auto a=s.car(),b=s.rival(0).car();a.orientation={1,0,0,0};a.velocity={40,0,0};
        b.orientation=a.orientation;b.velocity={-40,0,0};b.position=a.position;b.position.x+=4.2;
        a.rigid_body_ready=b.rigid_body_ready=false;a.collision_state=b.collision_state=0;
        check(s.vehicle_.start(a)&&s.rivals_[0]->vehicle_.start(b),"head-on queue fixture placed");
    }
    static void stop_crash(RaceSession& s,const Road& road) {
        ChaseCameraView result;
        s.crash_camera_.update(s.timing_.now_ticks,s.timing_.step,false,s.car(),s.spec_.handling,0,0,road,s.camera_world_,s.view_,result);
    }
    static void begin_crash(RaceSession& s,const Road& r) {
        auto c=s.car();c.position.z+=20;c.collision_state=1;c.published_support=0;c.rigid_body_ready=false;
        check(s.vehicle_.start(c),"airborne session placement accepted");
        s.crash_camera_.contact(35,true,s.timing_.now_ticks,true,c,0,r,s.camera_world_);
    }
};
}
int main(){
    World world;Road road;VehicleState c;c.collision_state=1;c.published_support=0;c.position={0,0,2};c.velocity={20,0,0};
    RaceCrashCamera camera;camera.configure(1003,128);ChaseCameraView view;
    camera.contact(34.999F,true,4000,true,c,0,road,world);
    check(camera.event()==0,"current impulse below35 does not request a shot");
    camera.contact(35,false,4000,true,c,0,road,world);
    check(camera.event()==0,"persistent contact is not another crash transition");
    camera.contact(35,true,4000,true,c,0,road,world);
    check(camera.event()==6&&camera.scale(1.F/60.F)==.4F,"threshold equality requests event6 and slow world time");
    check(camera.update(4000,0,true,c,stock_240sx_handling(),0,0,road,world,{},view),"active camera produces a view");
    check(view.fov==3640&&view.near_plane==.5&&view.far_plane==10000,"original camera lens and clip planes");
    OrbitCamera rendered;std::string render_error;CameraFrame render_frame;
    check(drive_render_camera(view,ChaseFovMode::vertical_4_3,1280,720,rendered,render_error)&&
        make_camera_frame(rendered,1280,720,render_frame,render_error),"cinematic view reaches the actual renderer adapter");
    check(std::abs(render_frame.forward.x-view.forward.x)<.0002&&std::abs(render_frame.forward.y-view.forward.z)<.0002&&
        std::abs(render_frame.forward.z+view.forward.y)<.0002,"renderer uses cinematic orientation rather than retained chase words");
    {
        AiRoadChain road_strip;int category=0;road.prepare(c,0,road_strip,category);
        check(crash_camera_road_visible(road_strip,{-2,0,2},{85,0,2},0,7),"original strip predicate accepts a line through every road width");
        check(!crash_camera_road_visible(road_strip,{-2,0,2},{85,50,2},0,7),"original strip predicate rejects a line cutting across road edges");
        struct RaisedTerrain:CrashCameraWorld {
            bool ground(const AiDriverPoint& p,float& h)const override{h=2.5F;return p[2]>=h;}
        } terrain;
        CrashCameraShot shot;
        check(crash_camera_road_shot(road_strip,category,1003,c,terrain,shot),"eye is raised before original terrain lookup");
    }
    c.published_support=4;
    check(camera.update(6000,1,true,c,stock_240sx_handling(),0,0,road,world,{},view),"half-second equality retains shot");
    check(!camera.update(6001,2,true,c,stock_240sx_handling(),0,0,road,world,{},view)&&camera.scale(1.F/60.F)==1,"four-wheel support after half-second restores time and view");
    c.published_support=0;
    camera.contact(50,true,23999,true,c,0,road,world);check(camera.event()==0,"five-second cooldown rejects just below boundary");
    camera.contact(50,true,24000,true,c,0,road,world);check(camera.event()==6,"five-second cooldown equality accepts");
    check(camera.update(35999,0,true,c,stock_240sx_handling(),0,20,road,world,{},view)&&camera.event()==6,"last tick below converted three-second threshold retains first shot");
    check(camera.update(36000,1,true,c,stock_240sx_handling(),0,20,road,world,{},view)&&camera.event()==7,"converted three-second threshold starts original second shot");
    check(!camera.update(36002,2,false,c,stock_240sx_handling(),0,20,road,world,{},view)&&camera.event()==0,"finish or disabled mode restores ordinary view immediately");
    camera.reset();camera.contact(35,true,4000,true,c,0,road,world);
    camera.begin_frame(8.F);check(camera.event()==6,"timeout equality preserves event");
    check(camera.scale(.001F)==1,"expiring frame uses unscaled world dt");
    camera.begin_frame(.001F);check(camera.event()==0,"timeout callback clears shot");
    road.available=false;camera.reset();camera.contact(50,true,4000,true,c,0,road,world);check(camera.event()==0,"missing valid road never invents a camera");road.available=true;
    // Real RaceSession: change slice count4->2->4 without losing a publication.
    std::string error;VehicleTrackCourse course;
    check(build_vehicle_proving_ground(course,error),"shared-world fixture loads");
    RaceCameraWorld native_ground(course.world);float ground_height=99.F;
    check(native_ground.ground({{100000.F,100000.F,20.F}},ground_height)&&ground_height==0.F,
        "original valid no-hit returns height zero instead of aborting camera prediction");
    TrackContactWorld invalid_ground;RaceCameraWorld invalid_camera_ground(invalid_ground);
    check(!invalid_camera_ground.ground({{0,0,1}},ground_height),"invalid ground adapter remains an error");
    check(!native_ground.ground({{std::numeric_limits<float>::infinity(),0,1}},ground_height),
        "nonfinite camera input remains an error");
    {
        auto airborne=c;airborne.position={100000,100000,3};airborne.velocity={40,0,0};
        airborne.owner_speeds.magnitude=40;airborne.published_support=0;airborne.collision_state=0;
        CrashCameraFlight predicted;
        check(crash_camera_predict(airborne,stock_240sx_handling(),2.24F,native_ground,predicted),
            "original jump prediction survives valid terrain misses along its flight");
        RaceCrashCamera jump;jump.configure(1003,kCrashCameraDefaultIntensity);
        check(jump.update(4000,5,true,airborne,stock_240sx_handling(),0,0,road,native_ground,{},view)&&jump.event()==6,
            "airborne periodic update requests a jump cinematic through the real ground adapter");
    }
    RaceSession session(course);
    std::vector<RaceRivalSetup> rivals(3);
    for(unsigned i=0;i<3;++i){rivals[i].spec=stock_240sx_vehicle_spec();rivals[i].pose=course.start;
        rivals[i].pose.ground.y+=8.F*(i+1);rivals[i].hull=stock_240sx_hull();rivals[i].name="rival"+std::to_string(i);}
    check(session.set_rivals(rivals,error),"three physical rivals configured");
    check(session.set_crash_camera(&road,128,1003,error)&&session.start(error),"production camera config starts");
    while(race_car_held(session.timing()))check(session.advance({},error),"grid advances");
    RaceWorldContactProbe::begin_crash(session,road);
    check(session.crash_camera_event()==6,"production owner accepts crash");
    const auto before=session.timing().now_ticks;const auto steps=session.work().physics_steps;
    const auto rival_steps=session.rival(0).work().physics_steps;
    for(unsigned i=0;i<8;++i){check(session.advance({},error),"slowed session advances every frame");check(session.telemetry().tick,"variable slice frame publishes route telemetry");}
    check(session.timing().now_ticks-before==8*27,"shared world advances27 ticks each slowed display frame");
    check(session.work().physics_steps-steps==8*2,"selector1 solves two smaller slices per slowed display frame");
    for(unsigned i=0;i<3;++i) {
        check(session.rival(i).timing().now_ticks==session.timing().now_ticks,"all rivals share the scaled world clock");
        check(session.rival(i).work().physics_steps-rival_steps==16,"all rival bodies retain per-display solver updates");
    }
    check(std::abs(session.world_frame_seconds()-float(1.F/60.F*.4F))<1e-9,"scaled world dt is retained by owner");
    for(float v:session.camera_velocity())check(std::abs(v)<.1F,"stationary roadside camera does not inherit a moving car's blur velocity");
    check(session.advance(race_input_frame({1,0,0},session.timing().step),error),"neutral pad connects before the held R3 edge");
    auto rear=race_input_frame({1,gamepad_button_bit(GamepadButton::stick_right),0},session.timing().step);
    check(session.advance(rear,error)&&session.crash_camera_event()==6,"held R3 does not replace the active cinematic");
    const auto return_clock=session.timing().now_ticks;const auto return_work=session.work().physics_steps;
    RaceWorldContactProbe::stop_crash(session,road);
    check(session.advance(rear,error),"ordinary world resumes after camera restoration");
    check(session.camera_velocity()==std::array<float,3>{},"cinematic return resets original view derivative");
    check(session.timing().now_ticks-return_clock==67&&session.work().physics_steps-return_work==4,"return restores normal world ticks and four solver slices");
    check(session.telemetry().tick&&session.camera().fov!=3640,"return publishes a normal chase view without dropping a route tick");
    check(session.camera().forward.x*chase_subject(session.car(),session.spec()).forward.x+session.camera().forward.y*chase_subject(session.car(),session.spec()).forward.y<0,"held R3 is restored as the current rear view");
    check(session.advance(race_input_frame({1,0,0},session.timing().step),error)&&session.camera().forward.x*chase_subject(session.car(),session.spec()).forward.x+session.camera().forward.y*chase_subject(session.car(),session.spec()).forward.y>0,"releasing R3 restores the selected follow view");
    {
        RaceSession impact(course);std::vector<RaceRivalSetup> one{rivals.front()};
        check(impact.set_rivals(one,error)&&impact.set_crash_camera(&road,128,1003,error)&&impact.start(error),"contact camera session starts");
        while(race_car_held(impact.timing()))check(impact.advance({},error),"contact fixture countdown");
        RaceWorldContactProbe::impact_placement(impact);
        check(impact.advance({},error),"actual contact queue advances");
        std::cout<<"CONTACT_CAMERA event="<<impact.crash_camera_event()<<" pairs="<<impact.pair_contacts().count<<" state="<<impact.car().collision_state<<'\n';
        check(impact.pair_impulses()>0&&impact.crash_camera_event()==6,"real car-pair response requests cinematic without a supplied trigger");
    }
    {
        RaceParticles particles;VehicleTrackTelemetry t;t.velocity={10,0,0};t.orientation.w=1;
        t.model.solver_dt=1.0/240;
        for(auto& wheel:t.model.wheels){wheel.contact=true;wheel.slip_ratio=1;}
        for(unsigned i=1;i<=8;++i){t.step=i;particles.advance(t,stock_240sx_vehicle_spec());}
        check(particles.smoke_count()>0,"particle fixture emitted smoke");
        const auto before_particle=particles.smoke(0);t.step=9;t.model.solver_dt=double(1.F/60.F*.4F)/2;
        for(auto& wheel:t.model.wheels)wheel.contact=false;
        particles.advance(t,stock_240sx_vehicle_spec());
        const double expected=double(8-before_particle.born_step)*before_particle.solver_dt+t.model.solver_dt;
        check(std::abs(particles.smoke(0).elapsed_seconds-expected)<1e-12,"existing smoke ages by actual slowed solver time across the transition");
    }
    {
        RaceSession sample(course);check(sample.start(error),"statistics timing fixture starts");
        RaceWorldContactProbe::statistics_clock(sample);RaceResultStatistics stats;
        for(unsigned i=1;i<=100;++i){const_cast<RaceTimingState&>(sample.timing()).step=i;stats.observe(sample);}
        const auto values=stats.values(sample,"240SX");
        check(values.at("Data_6")=="0.17 MI","post-race distance uses actual slowed physics time rather than display frames");
    }
    {
        RaceSession sample(course);RaceRivalSetup rival;
        rival.spec=stock_240sx_vehicle_spec();rival.hull=stock_240sx_hull();rival.pose=course.start;rival.pose.ground.y+=8;rival.name="statistics-rival";
        check(sample.set_rivals({rival},error)&&sample.start(error),"independent rival statistics fixture starts");
        RaceWorldContactProbe::statistics_clock(sample);
        const_cast<VehicleState&>(sample.rival(0).car()).velocity={100,0,0};RaceResultStatistics stats;
        for(unsigned i=1;i<=100;++i){auto& timing=const_cast<RaceTimingState&>(sample.timing());timing.step=i;stats.observe(sample);stats.observe(sample);}
        const auto human=stats.values(sample,"240SX"),other=stats.values(sample,"240SX",1);
        check(human.at("Data_4")=="894.80 MPH"&&other.at("Data_4")=="223.70 MPH","each racer has its own measured top speed");
        check(human.at("Data_6")=="0.17 MI"&&other.at("Data_6")=="0.04 MI","rival distance does not copy human or count repeated observations");
        check(other.at("RacerName")=="statistics-rival - 240SX","selected racer identity comes from its roster");
        auto& timing=const_cast<RaceTimingState&>(sample.timing());timing.racers[1].status=RacerStatus::totalled;
        const auto terminal=stats.values(sample,"240SX",1);
        check(terminal.at("Data_1")=="--:--.--"&&terminal.at("Data_2")=="Totaled","native49D4C0/49D5B0 no-time placeholder and terminal reason");
    }
    std::cout<<"crash camera tests: "<<failures<<" failures\n";return failures?1:0;
}
