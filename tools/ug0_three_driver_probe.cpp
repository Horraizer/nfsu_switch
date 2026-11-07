// PC integration/cost witness, not a complete original AI planner. Supplies
// declared straight launch paths to the three recovered drivers on real UG0
// grid/course. Never changes pose after start; all movement is selector3.
#include "nfsu/intro_grid.h"
#include "nfsu/player_powertrain.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include "ug0_path_choice_probe.h"
using namespace nfsu;
int main(int argc,char** argv) {
    if(argc<2||argc>6)return 2;
    const bool choose=argc>=3&&std::string(argv[2])=="--choose-paths";
    if(argc>=3&&!choose)return 2;
    unsigned frames=60;
    if(argc>=4){try{frames=unsigned(std::stoul(argv[3]));}catch(...){return 2;}}
    bool require_finish=false,retail_speed=false;
    for(int i=4;i<argc;++i) {
        if(std::string(argv[i])=="--require-finish")require_finish=true;
        else if(std::string(argv[i])=="--retail-speed")retail_speed=true;
        else return 2;
    }
    if(!frames||frames>36000)return 2;
    IntroGrid grid;std::string error;
    if(!load_intro_grid(argv[1],grid,error)){std::cerr<<error;return 1;}
    const IntroGridActor* player=nullptr;std::vector<RaceRivalSetup> setup;
    for(const auto& a:grid.actors) {
        if(a.config.role==Event0Role::opponent) {
            auto spec=a.placement_spec;spec.nitrous={};
            setup.push_back({spec,a.pose,a.hull,a.config.event_label});
        } else player=&a;
    }
    if(!player||setup.size()!=3){std::cerr<<"invalid UG0 grid";return 1;}
    RaceSession world(grid.course,player->placement_spec,RaceSteering::recovered_predicate1,
                       player->hull,RacePresentation::none);
    // Adaptive event tuning remains external. Retail-speed mode computes
    // original constructor low-slip from each actual selected placement setup.
    std::array<float,3> low_slip{{1,1,1}};
    if(retail_speed)for(unsigned i=0;i<3;++i) {
        float rating=0;
        if(!ai_setup_performance(setup[i].spec.handling,rating,low_slip[i])) {
            std::cerr<<"selected setup rating refused actor "<<i<<'\n';return 1;
        }
        std::cout<<std::setprecision(9)<<"SETUP actor "<<i<<" rating "<<rating<<" low_slip "<<low_slip[i]<<'\n';
    }
    if(!world.set_planned_rivals(setup,low_slip,error)||!world.start(error,&player->pose)) {
        std::cerr<<error;return 1;
    }
    unsigned held=0;
    while(race_car_held(world.timing())&&held++<1000)
        if(!world.advance({},error)){std::cerr<<error;return 1;}
    if(race_car_held(world.timing()))return 1;
    Ug0PathChoiceProbe planner;
    if(choose&&!planner.load(argv[1],grid,retail_speed)){std::cerr<<"planner data refused";return 1;}
    std::array<AiDriverPoint,3> initial{},direction{};
    for(unsigned i=0;i<3;++i) {
        const auto& s=world.rival(i).car();const auto& q=s.orientation;
        initial[i]={float(s.position.x),float(s.position.y),float(s.position.z)};
        direction[i]={float(1-2*(q.y*q.y+q.z*q.z)),float(2*(q.x*q.y+q.w*q.z)),float(2*(q.x*q.z-q.w*q.y))};
    }
    std::vector<double> milliseconds(frames);
    std::array<double,3> traveled{};
    auto last_position=initial;
    std::array<unsigned,3> stopped{},longest_stop{};
    std::array<unsigned,3> clear_frames{},recontacts{};
    std::array<unsigned,3> active_frames{};
    std::array<std::array<bool,4>,3> was_clear{};
    unsigned spike_reports=0;
    for(unsigned n=0;n<milliseconds.size();++n) {
        RaceRivalPlans batch;batch.step=world.timing().step;batch.count=3;
        for(unsigned i=0;i<3;++i) {
            auto& plan=batch.cars[i];const auto& car=world.rival(i).car();
            plan.path.count=3;plan.desired_speed=12;plan.route_distance=10;
            const AiDriverPoint position{{float(car.position.x),float(car.position.y),float(car.position.z)}};
            for(unsigned j=0;j<3;++j)for(unsigned k=0;k<3;++k)
                plan.path.points[j][k]=position[k]+20.F*j*direction[i][k];
        }
        std::array<AiPhysicsState,3> before;
        for(unsigned i=0;i<3;++i)before[i]=world.rival(i).ai_physics();
        for(unsigned i=0;i<3;++i)if(n&&world.rival(i).timing().outcome==RaceOutcome::running&&
            before[i].body.momentum!=world.rival(i).car().rigid_body.momentum) {
            const auto& public_body=world.rival(i).car().rigid_body;
            std::cerr<<std::setprecision(9)<<"EXTERNAL frame "<<n<<" actor "<<i;
            for(const auto& v:{before[i].body.position,public_body.position,before[i].body.velocity,public_body.velocity})
                for(float x:v)std::cerr<<' '<<x;
            std::cerr<<'\n';
        }
        const auto begin=std::chrono::steady_clock::now();
        if(choose&&!planner.plan(world,batch))return 1;
        std::array<bool,3> active{};
        for(unsigned i=0;i<3;++i) {
            active[i]=world.rival(i).timing().outcome==RaceOutcome::running;
            if(active[i])++active_frames[i];
        }
        if(!world.advance_planned({},batch,error)) {
            std::cerr<<"frame "<<n<<": "<<error<<'\n';
            for(unsigned i=0;i<3;++i) {
                const auto& r=world.rival(i);const auto& s=r.car();
                std::cerr<<"slot "<<i<<" name "<<world.rival_name(i)<<" position "<<s.position.x<<' '<<s.position.y<<' '<<s.position.z
                    <<" velocity "<<s.velocity.x<<' '<<s.velocity.y<<' '<<s.velocity.z<<" slices "<<r.ai_physics().slices<<'\n';
            }
            return 1;
        }
        milliseconds[n]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        for(unsigned j=0;j<world.pair_contacts().count;++j) {
            const auto& pair=world.pair_contacts().pairs[j];
            if(pair.contact.applied)std::cerr<<"PAIR frame "<<n<<" actors "<<pair.first<<' '<<pair.second
                <<" depth "<<pair.contact.depth<<" impulse "<<pair.contact.impulse[0]<<' '
                <<pair.contact.impulse[1]<<' '<<pair.contact.impulse[2]<<'\n';
        }
        for(unsigned i=0;i<3&&spike_reports<8;++i)if(n>0&&active[i]&&
            std::fabs(world.rival(i).ai_physics().body.velocity[2]-before[i].body.velocity[2])>20) {
            ++spike_reports;const auto& r=world.rival(i);const auto& t=r.telemetry();
            std::cerr<<std::setprecision(9)<<"VERTICAL_SPIKE frame "<<n<<" actor "<<i<<" hull_hits "<<t.hull_hits
                <<" impulses "<<t.impulses<<" impulse "<<t.impulse.x<<' '<<t.impulse.y<<' '<<t.impulse.z<<'\n';
            for(const auto& state:{before[i],r.ai_physics()}) {
                std::cerr<<"BODY";
                for(const auto& v:{state.body.position,state.body.velocity,state.body.force,state.body.torque,state.body.angular_velocity})
                    for(float x:v)std::cerr<<' '<<x;
                std::cerr<<'\n';
                std::cerr<<"AXES";for(const auto& axis:state.body.axes)for(float x:axis)std::cerr<<' '<<x;
                std::cerr<<'\n';
                auto body=state.body;auto engine=state.engine;body.force={};body.torque={};
                ai_powertrain_feedback(setup[i].spec.handling,body,engine);
                std::cerr<<"HELD_WHEEL_FORCE";for(float x:body.force)std::cerr<<' '<<x;
                const auto aero=player_aero_forces(setup[i].spec.handling,state.body,state.aero,setup[i].spec.aero);
                std::cerr<<" AERO";for(const auto& f:aero.forces)for(float x:f)std::cerr<<' '<<x;
                std::cerr<<'\n';
                for(const auto& w:state.engine.wheels)std::cerr<<"WHEEL "<<w.compression<<' '<<w.compression_rate<<' '<<w.load<<' '<<w.contact<<' '
                    <<w.excess<<' '<<w.normal[0]<<' '<<w.normal[1]<<' '<<w.normal[2]<<'\n';
            }
        }
        for(unsigned i=0;i<3;++i){
            if(!active[i])continue;
            const auto& s=world.rival(i).car();
            const double dx=s.position.x-last_position[i][0],dy=s.position.y-last_position[i][1];
            const double step=std::sqrt(dx*dx+dy*dy);traveled[i]+=step;
            last_position[i]={float(s.position.x),float(s.position.y),float(s.position.z)};
            stopped[i]=n>120&&step<.001?stopped[i]+1:0;longest_stop[i]=std::max(longest_stop[i],stopped[i]);
            bool any_clear=false;
            for(unsigned k=0;k<4;++k) {
                const bool clear=world.rival(i).telemetry().wheel_surface[k]==VehicleTrackSurface::clear;
                any_clear=any_clear||clear;
                if(was_clear[i][k]&&s.wheels[k].contact)++recontacts[i];
                was_clear[i][k]=clear;
            }
            if(any_clear)++clear_frames[i];
        }
        if(world.rival(0).progress().finished&&world.rival(1).progress().finished&&world.rival(2).progress().finished) {
            milliseconds.resize(n+1);break;
        }
    }
    const auto completed=static_cast<unsigned>(milliseconds.size());
    std::cout<<std::setprecision(10)<<"SCOPE three recovered drivers and selector3 bodies; "
        <<(choose?"retail road geometry, fitness, neighbor choice and pair prediction; supplied ordinary strategy/configuration/player intent":"supplied straight launch plans")
        <<"; no renderer or complete AI\n";
    std::cout<<"SPEED_POLICY "<<(retail_speed?"retail route/context/jitter; catchup1 and ordinary action flags supplied":"constant12 fixture")<<'\n';
    if(choose)std::cout<<"PLANNER decisions "<<completed*3<<" evaluations "<<planner.evaluations<<" pair_queries "<<planner.pairs
        <<" side_choices "<<planner.side_choices<<" fallback_choices "<<planner.fallbacks<<'\n';
    std::cout<<"WORLD frames "<<completed<<" budget "<<frames<<" required_laps "<<world.course().route.laps
        <<" driver_updates "<<world.work().rival_driver_updates
        <<" pairs "<<world.work().pair_candidates<<" impulses "<<world.pair_impulses()<<'\n';
    bool okay=world.work().rival_driver_updates==active_frames[0]+active_frames[1]+active_frames[2]&&
        world.work().pair_candidates==completed*6;
    for(unsigned i=0;i<3;++i) {
        const auto& r=world.rival(i);const auto& s=r.car();
        double distance=0;for(unsigned k=0;k<2;++k){const double d=s.rigid_body.position[k]-initial[i][k];distance+=d*d;}
        distance=std::sqrt(distance);
        const double height_change=s.position.z-initial[i][2];
        std::cout<<"RIVAL "<<i<<" name "<<world.rival_name(i)<<" distance "<<distance
            <<" traveled "<<traveled[i]<<" max_lap "<<r.progress().max_lap<<" longest_stop_frames "<<longest_stop[i]
            <<" clear_frames "<<clear_frames[i]<<" recontacts "<<recontacts[i]
            <<" finished "<<r.progress().finished<<" finish_step "<<r.progress().finish_step
            <<" active_frames "<<active_frames[i]
            <<" desired_min "<<(retail_speed?planner.minimum_speed[i]:12)<<" desired_max "<<(retail_speed?planner.maximum_speed[i]:12)
            <<" height_change "<<height_change
            <<" slices "<<r.ai_physics().slices<<" queries "<<r.ai_physics().ground_queries
            <<" resets "<<r.telemetry().resets<<" camera "<<r.work().camera_updates
            <<" pad "<<r.work().pad_updates<<" encode "<<r.work().record_encodes<<'\n';
        okay=okay&&distance>.1&&std::isfinite(distance)&&(frames!=60||(distance<10&&std::abs(height_change)<1))&&r.ai_physics().slices==active_frames[i]*2&&
            r.telemetry().resets==0&&r.work().camera_updates==0&&r.work().pad_updates==0&&r.work().record_encodes==0;
        if(require_finish)okay=okay&&r.progress().finished&&r.progress().max_lap==world.course().route.laps&&longest_stop[i]<600;
    }
    double sum=0;for(double value:milliseconds)sum+=value;
    std::sort(milliseconds.begin(),milliseconds.end());
    std::cout<<"PC_SIM_MS mean "<<sum/milliseconds.size()<<" p95 "<<milliseconds[(completed-1)*95/100]
        <<" max "<<milliseconds.back()<<"\nRESULT "<<(okay?"PASS":"FAIL")<<'\n';
    return okay?0:1;
}
