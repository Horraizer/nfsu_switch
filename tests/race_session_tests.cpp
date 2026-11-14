#include "nfsu/race_session.h"
#include "nfsu/race_traffic.h"
#include <iostream>
#include <limits>
#include <cmath>

namespace {
int failures=0;
void check(bool okay,const char* label) {
    if (!okay) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
}
}
namespace nfsu {
struct RaceWorldContactProbe {
    static RaceProp live_cube(unsigned slot,VehicleVec3 position,VehicleVec3 velocity,std::uint32_t tick) {
        VehicleHull hull;
        for(float x:{-1.F,1.F})for(float y:{-1.F,1.F})for(float z:{-1.F,1.F})
            hull.points.push_back({x,y,z});
        RaceProp p;
        p.spec=stock_240sx_vehicle_spec();
        p.spec.handling.centre_of_mass={};p.spec.handling.mass=2;p.spec.handling.inertia={1,1,1};
        p.points=hull.points;p.box=race_contact_box(hull);p.bounds=race_track_body_bounds(p.spec,p.box);
        p.state.position=position;p.state.velocity=velocity;p.state.orientation={1,0,0,0};
        vehicle_import_rigid(p.spec.handling,p.state);
        p.proxy_id=-1-int(slot);p.body_id=10+int(slot);p.world_slot=slot;
        p.impact={0,{.1F,.6F}};p.moved=p.awake=true;p.last_car_contact=tick;
        return p;
    }
    static bool prop_world(RaceSession& s,bool pair,std::string& error) {
        s.props_=std::make_unique<RaceProps>();
        auto& props=*s.props_;
        props.objects_.push_back(live_cube(0,{64,48,pair?24.0:11.02},pair?VehicleVec3{10,0,0}:VehicleVec3{0,0,-4},
            std::uint32_t(s.timing_.now_ticks)));
        if(pair)props.objects_.push_back(live_cube(1,{65.95,48,24},{},std::uint32_t(s.timing_.now_ticks)));
        s.world_shape_ids_.resize(props.objects_.size(),true);
        std::vector<RaceQueuedContact> trace;
        if(!s.resolve_world_contacts({race_contact_pose(s.car())},{},error,nullptr,nullptr,&trace))return false;
        const auto& first=props.objects_[0];
        if(pair)return std::any_of(trace.begin(),trace.end(),[](const RaceQueuedContact& hit){
                return hit.a>=10&&hit.b>=10&&hit.polygon==std::numeric_limits<std::size_t>::max();
            })&&first.state.velocity.x<10&&props.objects_[1].state.velocity.x>0;
        const bool supported=std::any_of(trace.begin(),trace.end(),[](const RaceQueuedContact& hit){
                return hit.a==10&&hit.polygon!=std::numeric_limits<std::size_t>::max();
            })&&first.state.velocity.z>-4&&first.terrain_contact&&!first.ground_contacts.empty();
        if(!supported)return false;
        for(unsigned frame=0;frame<180;++frame) {
            s.timing_.now_ticks+=67;
            if(!s.resolve_world_contacts({race_contact_pose(s.car())},{},error,nullptr,nullptr))return false;
            if(first.expired||!std::isfinite(first.state.position.z)||first.state.position.z<10.85||
               first.state.position.z>11.3)return false;
        }
        return true;
    }
    static bool proxy_reversion(RaceSession& s,std::string& error) {
        s.props_=std::make_unique<RaceProps>();
        auto p=live_cube(0,{64,48,24},{},std::uint32_t(s.timing_.now_ticks));
        p.moved=p.awake=false;p.registered_tick=std::uint32_t(s.timing_.now_ticks)-9000;
        p.placement_pose=race_contact_pose(p.state);p.placement_ready=true;
        p.state.position.z+=.1;
        s.props_->objects_.push_back(p);s.world_shape_ids_.resize(1,true);
        if(!s.resolve_world_contacts({race_contact_pose(s.car())},{},error,nullptr,nullptr))return false;
        const auto& restored=s.props_->objects_[0];
        return restored.body_id==-1&&restored.world_slot==32&&restored.state.position.z==24&&
            !s.world_shape_ids_[0];
    }
    static bool conversion_height(RaceSession& s,std::string& error) {
        RaceProps props;props.queued_contacts_=true;
        auto p=live_cube(0,{64,48,10.25},{},std::uint32_t(s.timing_.now_ticks));
        p.spec.handling.centre_of_mass={0,0,.25F};p.awake=false;
        props.convert_proxy(p,s.course_.world);
        const auto visual=race_prop_visual_position(p);
        if(std::abs(p.support_lift-1.05F)>1e-5F||visual[2]!=10.F||
            std::abs(p.state.position.z-11.3F)>1e-5F)return false;
        // No visual pop on conversion. Decay is after render publication and
        // stops on the same frame as settling, not a per-render frame drift.
        p.awake=true;p.state.rigid_body.gravity=false;
        if(!props.advance_body(p,s.course_.world,error,1.F/60.F))return false;
        if(p.render_support_lift!=1.05F||!(p.support_lift<p.render_support_lift))return false;
        props.restore_proxy(p);
        return p.support_lift==0&&p.render_support_lift==0&&p.state.position.z==10.25;
    }
    static bool settled_pool_ground(std::string& error,bool conversion=false) {
        VehicleTrackLayout layout;layout.name="layered-prop-ground";
        for(float height:{10.F,0.F}) {
            VehicleTrackQuad quad;const float xy[4][2]={{-5,-5},{5,-5},{5,5},{-5,5}};
            for(unsigned k=0;k<4;++k) {
                quad.corner[k][0]=xy[k][0];quad.corner[k][1]=xy[k][1];quad.corner[k][2]=height;
            }
            layout.quads.push_back(quad);
        }
        VehicleTrackCourse course;if(!build_vehicle_track_course(layout,course,error))return false;
        RaceProps props;props.queued_contacts_=true;props.clock_=1000;
        auto p=live_cube(0,{1,0,1},{},1);p.state.rigid_body.gravity=false;
        p.contact_count=1;p.first_contact_tick=1;p.sphere_radius=1;
        props.objects_.push_back(p);auto& live=props.objects_[0];
        TrackContact ground;
        if(conversion) {
            live.state.position.z=10.1;live.state.rigid_body.position[2]=10.1F;
            props.convert_proxy(live,course.world);
            live.state.position.z=1;live.state.rigid_body.position[2]=1;
        } else {
            if(!query_track_ground(course.world,{1,0,10.1},props.maintenance_ground_[0],ground)||
               ground.point[2]!=10.F)return false;
            if(!props.advance_body(live,course.world,error,1.F/60.F)||live.awake)return false;
        }
        props.maintain_pool(course.world,live.state.position,1000,51);
        return !live.expired&&query_track_ground(course.world,live.state.position,
            props.maintenance_ground_[0],ground)&&ground.point[2]==0.F;
    }
    static bool queued_scenery(RaceSession& s,std::string& error,bool pressure=false) {
        VehicleHull hull;
        for(float x:{-1.F,1.F})for(float y:{-1.F,1.F})for(float z:{-1.F,1.F})
            hull.points.push_back({x,y,z});
        auto car=s.car();
        car.position.z+=10;car.orientation={1,0,0,0};car.velocity={20,0,0};
        car.simulation_seconds=kDriveFrameSeconds;car.rigid_body_ready=false;
        if(!s.vehicle_.start(car))return false;
        s.contact_box_=race_contact_box(hull);s.contact_bounds_=race_track_body_bounds(s.spec_,s.contact_box_);
        auto history=race_contact_pose(s.car());history.time=0;history.position.x-=.32;
        RaceProp prop;prop.proxy_id=prop.body_id=-5;prop.impact={.2F,{.1F,.6F}};
        prop.spec=stock_240sx_vehicle_spec();prop.spec.handling.centre_of_mass={};
        prop.box=race_contact_box(hull);prop.points=hull.points;
        prop.bounds=race_track_body_bounds(prop.spec,prop.box);
        prop.state.position=s.car().position;
        prop.state.position.x+=1.9-s.spec_.handling.centre_of_mass[0];
        prop.state.orientation={1,0,0,0};
        vehicle_import_rigid(prop.spec.handling,prop.state);
        const float x=float(prop.state.position.x),y=float(prop.state.position.y);
        prop.placement_bounds={x-1,y-1,x+1,y+1};
        s.props_=std::make_unique<RaceProps>();
        auto& props=*s.props_;
        props.objects_.reserve(33);props.objects_.push_back(prop);props.proxy_order_={0};props.widest_proxy_=2;
        if(pressure) {
            // Reachable after32 moved, sleeping zero-lifetime objects. Their
            // positions are far from the human but do not block a fresh prop.
            for(unsigned slot=0;slot<32;++slot) {
                auto old=live_cube(slot,{500+double(slot)*3,500,24},{},10000);
                old.awake=false;props.objects_.push_back(std::move(old));
            }
            s.world_shape_ids_.assign(32,true);
        }
        props.terrain_queries_.resize(hull.points.size());props.terrain_hits_.resize(hull.points.size());
        props.terrain_statuses_.resize(hull.points.size());props.terrain_arms_.resize(hull.points.size());
        std::vector<RaceQueuedContact> trace;
        if(!s.resolve_world_contacts({history},{},error,nullptr,nullptr,&trace))return false;
        const auto& live=props.objects_[0];
        const auto& hits=s.prop_contacts();
        const bool reclaimed=!pressure||std::all_of(props.objects_.begin()+1,props.objects_.end(),
            [](const RaceProp& p){return p.expired&&p.body_id<0;});
        const bool okay=reclaimed&&live.moved&&live.body_id>=0&&live.impact.activation==0&&
            hits.count>=2&&hits.hits[0].id==0&&hits.hits[1].id==0&&
            hits.hits[0].audio_order<hits.hits[1].audio_order&&
            !trace.empty()&&trace.front().prop==0&&trace.front().remaining>0;
        if(!okay) {
            RaceHullQuery query;
            const bool queried=query_race_hulls(s.car(),s.spec_,s.contact_box_,live.state,live.spec,live.box,query);
            std::cerr<<"prop queue diagnostic "<<hits.count<<' '<<live.moved<<' '<<live.body_id<<' '
                <<live.impact.activation<<' '<<trace.size()<<' '<<(trace.empty()?0:trace.front().remaining)<<' '
                <<props.candidates<<' '<<s.car().simulation_seconds<<' '<<s.world_contact_pairs_.size()<<' '
                <<queried<<' '<<query.distance<<'\n';
        }
        return okay;
    }
    static bool registered_grid_tie(RaceSession& s,std::string& error) {
        auto a=s.car(),b=s.rival(0).car();
        a.orientation=b.orientation={1,0,0,0};
        a.velocity=b.velocity={};
        a.position.z+=10;
        b.position=a.position;b.position.y+=3;
        a.rigid_body_ready=b.rigid_body_ready=false;
        if(!s.vehicle_.start(a)||!s.rivals_[0]->vehicle_.start(b))return false;
        const std::array<RaceContactPose,4> before{{race_contact_pose(s.car()),race_contact_pose(s.rival(0).car())}};
        if(!s.resolve_world_contacts(before,{},error,nullptr,nullptr))return false;
        for(const auto& pair:s.world_contact_pairs_)
            if(!pair.track())return pair.a==1&&pair.b==0;
        return false;
    }
    static bool retirement_contact(RaceSession& s,bool retired,std::string& error,bool recovering=false) {
        auto a=s.car(),b=s.rival(0).car();a.orientation={1,0,0,0};a.velocity={40,0,0};
        b.orientation=a.orientation;b.velocity={-40,0,0};b.position=a.position;b.position.x+=4.2;
        a.rigid_body_ready=b.rigid_body_ready=false;a.collision_state=b.collision_state=0;
        if(!s.vehicle_.start(a)||!s.rivals_[0]->vehicle_.start(b))return false;
        if(retired) {
            auto t=s.timing_;t.racers[0].new_lap=true;t.racers[0].highest_lap=1;
            const unsigned order[]={0,1};
            if(!apply_lap_knockout(s.timing_config_,t,order,2,error))return false;
            s.timing_.racers[1]=t.racers[1];
        }
        if(!recovering)return s.advance({},error);
        const auto start=s.timing().now_ticks-5900;
        s.vehicle_.owner_recovery_start_=start;
        if(!s.advance({},error)||!s.vehicle_.last_ghost_overlap_||s.pair_impulses()!=0)return false;
        s.vehicle_.extend_owner_recovery(start+6100);
        return s.vehicle_.owner_recovery_active(start+8000)&&
            !s.vehicle_.owner_recovery_active(start+10000);
    }
};
}
int main() {
    using namespace nfsu;
    std::string error; std::vector<RaceInputRun> runs;
    check(RaceWorldContactProbe::settled_pool_ground(error),
          "settled prop discards upper-road cache before below-road reclamation");
    check(RaceWorldContactProbe::settled_pool_ground(error,true),
          "prop conversion does not seed persistent upper-road maintenance cache");
    for (const auto* text : {"", "NFSU_RACE_INPUT 2\n1 0 0\n", "NFSU_RACE_INPUT 1\n0 0 0\n",
        "NFSU_RACE_INPUT 1\n120000 0 0\n1 0 0\n", "NFSU_RACE_INPUT 1\n1 0 32768\n",
        "NFSU_RACE_INPUT 1\n1 -1 0\n", "NFSU_RACE_INPUT 1\n1 0 0 extra\n"})
        check(!parse_race_input(text,runs,error),"malformed input refused");
    check(!parse_race_input("NFSU_RACE_INPUT 1\n1 " + std::to_string(kGamepadButtonMask+1U) + " 0\n",runs,error),
          "unknown button bit refused");
    check(!parse_race_input(std::string("NFSU_RACE_INPUT 1\n1 0 0\n")+char(128),runs,error),"non-ASCII refused");
    check(parse_race_input("# controller only\nNFSU_RACE_INPUT 1\r\n3 0 -32768\n2 0 32767\n",runs,error),"limits accepted");
    const auto canonical=format_race_input(runs);
    check(parse_race_input(canonical,runs,error) && format_race_input(runs)==canonical,"recording round trip");
    std::vector<RaceInputRun> compressed;
    for (int i=0;i<3;++i) append_race_input(compressed,race_input_frame(runs[0],i));
    check(compressed.size()==1 && compressed[0].ticks==3 && compressed[0].steer==-32768,"RLE keeps full steering range");

    {
        RaceTrafficActor traffic;
        RaceTraffic::contact(traffic,0,{5,0,0},10);
        check(traffic.impacted&&!traffic.crash_stop,"light traffic hit selects physics without permanent stop");
        RaceTraffic::contact(traffic,0,{25,0,0},11);
        check(!traffic.crash_stop,"traffic stop threshold is strictly greater than30");
        RaceTraffic::contact(traffic,1,{1,0,0},11);
        check(!traffic.crash_stop,"unrelated contacts do not share severity");
        RaceTraffic::contact(traffic,0,{1,0,0},12);
        check(traffic.crash_stop,"retained same-pair severity stops traffic");
        RaceTrafficActor expired;
        RaceTraffic::contact(expired,0,{20,0,0},10);
        RaceTraffic::contact(expired,0,{20,0,0},29);
        check(!expired.crash_stop,"expired traffic contact does not accumulate forever");
    }
    VehicleTrackCourse course;
    check(build_vehicle_proving_ground(course,error),"synthetic course builds");
    {
        RaceSession session(course);
        check(session.start(error)&&RaceWorldContactProbe::proxy_reversion(session,error),
            "unused converted prop restores its authored pose and releases its body ID");
    }
    {
        RaceSession session(course);
        check(session.start(error)&&RaceWorldContactProbe::conversion_height(session,error),
            "prop conversion lifts collision support without a visual jump and restores its proxy");
    }
    for(bool pair:{false,true}) {
        RaceSession session(course);
        const bool okay=session.start(error)&&RaceWorldContactProbe::prop_world(session,pair,error);
        if(!okay&&!error.empty())std::cerr<<error<<'\n';
        check(okay,pair?"shared world resolves two free props":"shared world supports a falling prop on a road polygon");
    }
    {
        RaceSession session(course);
        const bool okay=session.start(error)&&RaceWorldContactProbe::queued_scenery(session,error);
        if(!okay&&!error.empty())std::cerr<<error<<'\n';
        check(okay,"world queue converts and sweeps a prop and publishes both activation impulses");
    }
    {
        const RaceRivalSetup rival{stock_240sx_vehicle_spec(),course.start,stock_240sx_hull(),"RIVAL"};
        RaceSession session(course);
        check(!session.set_rivals({rival},error,RacePresentation::none,3),"grid registration outside roster refused");
        check(session.set_rivals({rival},error,RacePresentation::none,2)&&session.start(error)&&
            RaceWorldContactProbe::registered_grid_tie(session,error),
            "equal bounds admit the earlier grid registration as the first body");
    }
    {
        RaceSession session(course,stock_240sx_vehicle_spec(),RaceSteering::recovered_predicate1);
        bool okay=session.start(error);
        unsigned frames=0;
        while(okay&&race_car_held(session.timing())&&frames++<1000)
            okay=session.advance(race_input_frame({1,0,0},frames),error);
        check(okay&&!race_car_held(session.timing()),"owner pedal fixture reaches GO");
        if(okay&&!race_car_held(session.timing())) {
            const auto brake=race_input_frame({1,gamepad_button_bit(GamepadButton::trigger_left),0},0);
            const auto throttle=race_input_frame({1,gamepad_button_bit(GamepadButton::trigger_right),0},0);
            okay=session.advance(brake,error);
            check(okay&&session.car().transmission.requested_gear==kVehicleGearReverse,
                "brake requests reverse on the first owner update at rest");
            unsigned reverse_frames=0;
            while(okay&&session.car().owner_speeds.forward>-2.1F&&reverse_frames++<180)
                okay=session.advance(brake,error);
            check(okay&&session.car().gear==kVehicleGearReverse&&session.car().owner_speeds.forward<=-2.1F,
                "clutch engages reverse and the car backs up");
            okay=okay&&session.advance(throttle,error);
            check(okay&&session.car().transmission.requested_gear==kVehicleGearFirst&&session.telemetry().model.brake>.9,
                "accelerator restores automatic first and brakes a car still backing up");
            unsigned forward_frames=0;
            while(okay&&session.car().owner_speeds.forward<1.F&&forward_frames++<240)
                okay=session.advance(throttle,error);
            check(okay&&session.car().gear>=kVehicleGearFirst&&session.car().owner_speeds.forward>=1.F,
                "holding the accelerator completes the forward transition");
        }
    }

    {
        auto spec=stock_240sx_vehicle_spec();for(auto& torque:spec.handling.torque)torque=2.F;
        RaceSession session(course,spec);
        check(session.drag_category()==0,"ordinary racer publishes its high-performance ride class before drag setup");
        check(session.set_drag(true,error)&&session.drag_category()==0,
            "ordinary and drag use the same installed-car performance class");
    }
    {
        RaceSession session(course);std::vector<RaceRivalSetup> rivals(3);
        for(unsigned i=0;i<3;++i) {
            auto& r=rivals[i];r.pose=course.start;r.pose.ground.y+=double(i+1)*2.5;
            r.spec=stock_240sx_vehicle_spec();r.hull=stock_240sx_hull();r.name="world-derivative-rival";
        }
        bool okay=session.set_rivals(rivals,error)&&session.start(error);
        unsigned frames=0;
        while(okay&&race_car_held(session.timing())&&frames++<1000)okay=session.advance({},error);
        check(okay&&!race_car_held(session.timing()),"four-actor derivative fixture starts");
        bool moving=false;
        for(unsigned frame=0;okay&&frame<40;++frame) {
            std::array<VehicleVec3,4> before{};std::array<std::uint64_t,4> resets{};
            before[0]=session.car().velocity;resets[0]=session.telemetry().resets;
            for(unsigned i=0;i<3;++i){before[i+1]=session.rival(i).car().velocity;resets[i+1]=session.rival(i).telemetry().resets;}
            const float dt=session.next_world_frame_seconds();
            okay=session.advance(race_input_frame({1,gamepad_button_bit(GamepadButton::trigger_right),0},0),error);
            if(!okay)break;
            for(unsigned i=0;i<4;++i) {
                const auto& actor=i?session.rival(i-1):session;
                const auto& v=actor.car().velocity;const auto& a=actor.car().owner_motion.acceleration;
                const bool reset=actor.telemetry().resets!=resets[i];const float inverse=1.F/dt;
                const std::array<float,3> expected{{reset?0.F:(float(v.x)-float(before[i].x))*inverse,
                    reset?0.F:(float(v.y)-float(before[i].y))*inverse,reset?0.F:(float(v.z)-float(before[i].z))*inverse}};
                check(a==expected,"world owner publishes all four post-contact acceleration caches");
                if(i==0)moving=moving||a[0]!=0.F||a[1]!=0.F||a[2]!=0.F;
            }
        }
        check(okay&&moving,"world derivative producer executes on moving actors");
    }
    {
        for(bool retired:{false,true}) {
            RaceSession session(course);RaceRivalSetup r;r.pose=course.start;r.spec=stock_240sx_vehicle_spec();r.hull=stock_240sx_hull();r.name="retirement-rival";
            const bool ready=session.set_rivals({r},error)&&session.start(error);
            check(ready,"retirement contact fixture starts");if(!ready)continue;
            unsigned grid_frames=0;
            while(race_car_held(session.timing())&&grid_frames++<1000) {
                if(!session.advance({},error)){check(false,"retirement countdown advances");break;}
            }
            check(!race_car_held(session.timing()),"retirement countdown bounded");
            if(race_car_held(session.timing()))continue;
            check(RaceWorldContactProbe::retirement_contact(session,retired,error),"retirement head-on world queue advances");
            check((session.pair_impulses()>0)==!retired,"native ghost stops car response without deleting the stopped actor");
            check(session.rival_count()==1&&session.actor_ghost(1)==retired,"retired physical actor stays allocated");
            if(retired) {
                check(session.actor_visible(1)==((session.timing().step&1U)==0),"retired actor blink follows render frames");
                for(unsigned i=0;i<125;++i) {
                    if(!session.advance({},error)){std::cerr<<"retirement fixture: "<<error<<'\n';check(false,"retired car continues track simulation");break;}
                }
                check(!session.actor_visible(1),"retired actor stays hidden after original interval");
                RaceSession restarted(course);
                check(restarted.set_rivals({r},error)&&restarted.start(error)&&!restarted.actor_ghost(1)&&restarted.actor_visible(1),
                    "fresh race owner clears actor retirement");
            }
        }
    }

    {
        RaceSession session(course);
        RaceRivalSetup rival;
        rival.pose=course.start;rival.spec=stock_240sx_vehicle_spec();
        rival.hull=stock_240sx_hull();rival.name="recovery-contact-rival";
        bool okay=session.set_rivals({rival},error)&&session.start(error);
        unsigned frames=0;
        while(okay&&race_car_held(session.timing())&&frames++<1000)okay=session.advance({},error);
        check(okay&&!race_car_held(session.timing())&&
            RaceWorldContactProbe::retirement_contact(session,false,error,true),
            "overlapping ghost pair skips impulse and extends the recovery window");
    }

    {
        RaceSession terminal(course);check(terminal.start(error),"terminal placement fixture starts");
        auto& timing=const_cast<RaceTimingState&>(terminal.timing());timing.racers.resize(4);
        timing.racers[0].status=RacerStatus::blown_engine;timing.racers[0].position=4;
        timing.racers[2].status=RacerStatus::normal_completion;timing.racers[2].position=1;
        auto& order=const_cast<RaceStandings&>(terminal.standings());order.count=4;order.order={{0,2,3,1}};
        check(terminal.result_positions()==std::array<unsigned,4>{{4,3,1,2}},
              "DNF last place and finished winner stay reserved; unfinished rivals fill remaining places");
        const auto rows=terminal.result_rows();
        check(rows.size()==4&&rows[0].racer==2&&rows[1].racer==3&&rows[1].position==2&&
              !rows[1].total_valid&&rows[1].status==RacerStatus::not_finished&&rows[2].racer==1,
              "post-race rows use classified places without fabricating a finish");
        timing.racers[3].position=2;timing.racers[1].position=3;
        check(terminal.result_positions()==std::array<unsigned,4>{{4,3,1,2}},"complete result permutation ignores stale progress order");
    }
    // Headless simulation must retain mechanical/timing results, and records
    // are serialized only on demand. A player camera is not rival simulation.
    {
        RaceSession visible(course),headless(course,stock_240sx_vehicle_spec(),
            RaceSteering::baseline,stock_240sx_hull(),RacePresentation::none);
        check(visible.start(error)&&headless.start(error),"headless comparison starts");
        for(unsigned tick=0;tick<480;++tick) {
            const auto p=race_input_frame({1,tick>200?
                gamepad_button_bit(GamepadButton::trigger_right):0,0},tick);
            const auto old_eye=visible.camera().eye;const auto resets=visible.telemetry().resets;
            check(visible.advance(p,error)&&headless.advance(p,error),"headless comparison steps");
            const auto eye=visible.camera().eye;
            const auto expected=camera_motion_velocity({{float(eye.x),float(eye.y),float(eye.z)}},
                {{float(old_eye.x),float(old_eye.y),float(old_eye.z)}},float(kDriveFrameSeconds),visible.telemetry().resets!=resets);
            check(visible.camera_velocity()==expected,"live NOS source follows the published view, not body velocity");
            for(std::size_t k=0;k<kDriveStepsPerFrame;++k) {
                std::array<std::uint8_t,kVehicleTrackTelemetryRecordBytes> va{},vb{};
                encode_vehicle_track_telemetry(visible.physics_telemetry()[k],va);
                encode_vehicle_track_telemetry(headless.physics_telemetry()[k],vb);
                check(va==vb,"headless preserves complete mechanical telemetry");
            }
            check(visible.timing().race_ticks==headless.timing().race_ticks,
                  "headless preserves race time");
        }
        check(headless.work().camera_updates==0&&visible.work().camera_updates==480,
              "only the local player updates a camera");
        check(headless.work().record_encodes==0,"unobserved records cost no encoding");
        const auto bytes=headless.record();
        check(!bytes.empty()&&bytes==headless.record()&&headless.work().record_encodes==1,
              "repeated observation encodes once");
        check(headless.advance({},error)&&headless.work().record_encodes==1,
              "next advance only invalidates recording");
        check(headless.record()!=bytes&&headless.work().record_encodes==2,
              "next observation returns current frame");
        VehicleInput direct;direct.throttle=.5;direct.road_steering=true;
        direct.road_angle_word_valid=true;direct.road_angle_word=512;
        const auto pads=headless.work().pad_updates;
        check(headless.advance_driver(direct,error),"processed driver reaches physics");
        check(headless.work().pad_updates==pads&&headless.work().direct_updates==1,
              "processed driver bypasses gamepad");
        check(headless.car().steering_word==512,"processed steering word is not player-filtered");
        direct.throttle=0;direct.brake=.625;
        check(headless.advance_driver(direct,error)&&headless.light_service_brake()==.625,
              "direct brake updates brake lights without pad state");
        check(!visible.advance_driver(direct,error),"direct driver cannot replace player controls");
    }
    {
        RaceSession ai(course,stock_240sx_vehicle_spec(),RaceSteering::baseline,
                       stock_240sx_hull(),RacePresentation::none);
        check(ai.configure_ai(1,error)&&ai.start(error),"selector3 session starts");
        VehicleInput in;in.road_steering=true;in.road_angle_word_valid=true;
        unsigned count=0;
        while(race_car_held(ai.timing())&&count++<1000)
            if(!ai.advance_driver(in,error)){check(false,"selector3 countdown");break;}
        check(ai.ai_physics().slices==0,"selector3 countdown holds physical state");
        check(!ai.advance({},error),"selector3 refuses player pad adapter");
        in.throttle=.5;
        const auto start=ai.car().position;
        bool good=true;
        for(unsigned n=0;n<60;++n)if(!ai.advance_driver(in,error)){good=false;break;}
        if(!good)std::cerr<<error<<'\n';
        check(good,"selector3 advances through track session");
        check(ai.ai_physics().slices==120&&ai.work().physics_steps==120,
              "selector3 uses source two slices per world tick");
        check(ai.work().camera_updates==0&&ai.work().pad_updates==0&&ai.work().record_encodes==0,
              "selector3 has no rival camera, pad or serialization work");
        check(ai.car().position.x!=start.x||ai.car().position.y!=start.y,"selector3 actually moves");
        check(ai.car().rigid_body_ready&&ai.car().steering_word_ready,
              "selector3 publishes current rigid pose and steering");
        const auto step=ai.car().step;
        check(!ai.advance_driver(in,error,2)&&ai.car().step==step,
              "unsupported AI gear request preserves race frame");
        check(ai.advance_driver(in,error,0)&&ai.car().gear==0,
              "processed reverse request reaches selector3 transmission");
        check(ai.advance_driver(in,error,-1)&&ai.car().gear>=2,
              "processed automatic request releases reverse through session");
    }
    {
        RaceSession world(course);
        std::vector<RaceRivalSetup> setup;
        for(unsigned i=0;i<3;++i) {
            auto pose=course.start;pose.ground.x+=12*(i+1);
            if(i==2)pose.heading=vehicle_track_heading(16384);
            setup.push_back({stock_240sx_vehicle_spec(),pose,stock_240sx_hull(),"rival"+std::to_string(i)});
        }
        check(!world.set_planned_rivals(setup,{{1,0,1}},error)&&world.rival_count()==0,
              "invalid second rival does not leave a partial grid");
        check(world.set_planned_rivals(setup,{{1,1,1}},error)&&world.start(error),"three original drivers start");
        unsigned count=0;
        while(race_car_held(world.timing())&&count++<1000)
            if(!world.advance({},error)){check(false,"three rival countdown");break;}
        check(world.work().rival_driver_updates==0,"countdown does not integrate driver history");
        const auto step=world.timing().step,frames=world.work().frames;
        check(!world.advance({},error)&&world.timing().step==step&&world.work().frames==frames,
              "missing planner never runs test driver or advances world");
        RaceRivalPlans batch;batch.count=3;batch.step=step;
        for(unsigned i=0;i<3;++i) {
            auto& p=batch.cars[i];const auto& s=world.rival(i).car();
            p.path.count=3;p.desired_speed=i==1?0:12;p.route_distance=10;
            for(unsigned j=0;j<3;++j)p.path.points[j]={float(s.position.x)+(i==2?0:20*j),
                float(s.position.y)+(i==2?20*j:0),float(s.position.z)};
        }
        auto bad=batch;bad.cars[2].desired_speed=std::numeric_limits<float>::quiet_NaN();
        check(!world.advance_planned({},bad,error)&&world.timing().step==step&&
              world.rival(0).ai_physics().slices==0&&world.work().rival_driver_updates==0,
              "invalid last plan refuses before any rival advances");
        const auto initial0=world.rival(0).car().position.x,initial1=world.rival(1).car().position.x;
        bool good=true;
        for(unsigned n=0;n<30;++n) {
            batch.step=world.timing().step;
            if(!world.advance_planned({},batch,error)){good=false;break;}
            if(n==0)check(world.rival(2).car().steering_word==0,
                "first driver frame uses placed heading before component-body import");
        }
        if(!good)std::cerr<<error<<'\n';
        check(good&&world.work().rival_driver_updates==90,"three drivers update once per moving world frame");
        check(world.work().pair_candidates==0,"separated hull bounds avoid narrow queries");
        check(world.pair_contacts().count==0&&world.pair_contacts().step+1==world.timing().step,
              "empty resolved-job batch retains the completed world epoch");
        for(unsigned i=0;i<3;++i) {
            const auto& r=world.rival(i);
            check(r.ai_physics().slices==60&&r.timing().step==world.timing().step,
                  "three selector3 bodies share the world cadence");
            check(r.work().camera_updates==0&&r.work().pad_updates==0&&r.work().record_encodes==0,
                  "original rivals have no camera, pad or encoding overhead");
        }
        check(world.rival(0).car().position.x>initial0+.1&&
              std::abs(world.rival(1).car().position.x-initial1)<.01,
              "separate driver targets move one rival and hold another");
        const auto current=world.timing().step;
        check(!world.advance_planned({},batch,error)&&world.timing().step==current,
              "stale planner packet cannot advance a second time");
    }
    RaceSession a(course),b(course);
    check(race_session_hud(a)==std::vector<std::string>{"RACE HUD UNAVAILABLE"},"invalid HUD handled");
    check(!a.advance({},error),"unstarted race refused");
    check(a.start(error) && b.start(error),"sessions start");
    check(!a.start(error),"race is one shot");
    const auto origin=a.car().position;
    int countdown=0;
    while (race_car_held(a.timing()) && countdown<1000) {
        const auto pad=race_input_frame({1,0,0},countdown++);
        check(a.advance(pad,error) && b.advance(pad,error),"countdown steps");
        check(a.car().position.x==origin.x && a.car().position.z==origin.z,"countdown holds grid including gravity");
        check(a.car().step==0 && a.car().simulation_seconds>0,
              "held countdown advances engine time without body steps");
        check(a.record()==b.record(),"countdown deterministic");
    }
    check(countdown>0 && countdown<1000,"countdown reaches GO");
    check(race_session_hud(a)[2]=="GO","GO is a message, not countdown 4");
    const auto gas=gamepad_button_bit(GamepadButton::trigger_right);
    for (int i=0;i<120;++i) {
        auto pad=race_input_frame({1,gas,0},countdown+i);
        check(a.advance(pad,error) && b.advance(pad,error),"driving steps");
        check(a.record()==b.record(),"every integrated step is deterministic");
        if(i==0) {
            check(a.physics_telemetry().size()==4,"original ordinary branch exposes four slice records");
            for(std::size_t k=0;k<a.physics_telemetry().size();++k) {
                const auto& slice=a.physics_telemetry()[k];
                check(slice.model.solver_dt==1.0/240.0,"race uses source-backed solver interval");
                check(slice.tick==(k==3),"track checks run once after the fourth slice");
            }
        }
    }
    check(a.car().position.x>origin.x+1,"neutral countdown then gas really moves the car");
    check(a.telemetry().step==480,"four collision slices per input tick preserve two seconds of race time");
    // Countdown gas, release, neutral cutoff and GO preserve the same
    // physical engine used by HUD/audio and the driving drivetrain.
    {
        RaceSession rev(course), idle(course);
        check(rev.start(error)&&idle.start(error),"grid engine sessions start");
        check(rev.advance(race_input_frame({1,0,0},0),error)&&
              idle.advance(race_input_frame({1,0,0},0),error),"connect pad before gas");
        const auto pose=rev.car().position;
        double peak=0, released=0;
        bool cutoff=false;unsigned n=1;
        while(race_car_held(rev.timing())&&n<1000) {
            const bool gas_on=n<90 || n>=130;
            check(rev.advance(race_input_frame({1,gas_on?gas:0,0},n),error)&&
                  idle.advance(race_input_frame({1,0,0},n),error),"grid throttle script");
            check(rev.car().position.x==pose.x&&rev.car().position.y==pose.y&&
                  rev.car().position.z==pose.z&&rev.car().step==0,"revving cannot move grid pose");
            check(rev.car().gear==kVehicleGearNeutral,"grid stays neutral");
            for(const auto& w:rev.car().wheels)check(w.spin==0,"revving cannot spin wheels");
            peak=std::max(peak,rev.car().engine_rpm);
            cutoff=cutoff||rev.car().engine.cutoff_until!=0;
            if(n==129)released=rev.car().engine.angular_speed;
            if(n==90)check(rev.telemetry().model.throttle>0.9 &&
                          rev.telemetry().model.throttle<1,"neutral release retains decaying throttle");
            if(n==129)check(rev.telemetry().model.throttle<0.001,"neutral release completes over time");
            ++n;
        }
        check(peak>idle.car().engine_rpm+2000,"countdown throttle raises actual engine RPM");
        check(cutoff,"countdown uses engine rev limiter");
        check(released*9.54930400848388671875<peak,"release slows engine from its peak");
        const auto go_rpm=rev.car().engine.angular_speed;
        check(go_rpm>idle.car().engine.angular_speed,"GO retains revved engine");
        for(unsigned j=0;j<120;++j)
            check(rev.advance(race_input_frame({1,gas,0},n+j),error),"revved launch steps");
        check(rev.car().gear>=kVehicleGearFirst&&rev.car().position.x>pose.x+1,
              "GO engages drivetrain and launches");
        std::cout << "GRID_ENGINE peak_rpm=" << peak << " go_rad_s=" << go_rpm
                  << " released_rad_s=" << released << '\n';
    }
    const auto prior=a.camera().pov;
    auto pad=race_input_frame({1,gamepad_button_bit(GamepadButton::face_north)|gamepad_button_bit(GamepadButton::select),0},500);
    check(a.advance(pad,error),"camera and manual reset accepted");
    check(a.camera().pov!=prior,"camera cycles once");
    check(a.telemetry().resets==1,"manual reset reaches collision session");
    for(std::size_t k=0;k<a.physics_telemetry().size();++k)
        check((a.physics_telemetry()[k].reset!=VehicleTrackReset::none)==(k==3),
              "manual reset runs only at the final outer slice");
    check(a.car().step==0 && a.car().simulation_seconds==0,
          "manual reset restarts the mechanical clock");
    check(a.advance(pad,error) && a.telemetry().resets==1,"holding reset does not reset every tick");
    check(a.car().step==4 && std::abs(a.car().simulation_seconds-1.0/60.0)<1e-12,
          "held reset resumes exactly one outer interval");
    check(race_session_hud(a).size()==4,"minimal HUD available");
    // R3 is held, not a POV cycle. The normal camera must keep following
    // during the hold and return immediately to its current selected view.
    {
        DriveControls controls;
        const auto rear=gamepad_button_bit(GamepadButton::stick_right);
        auto p=race_input_frame({1,rear,0},0);
        controls.update(p);
        check(!controls.frame_state().look_back,"R3 hot-plug hold remains latched");
        p.pads[0].buttons=0;controls.update(p);
        p.pads[0].buttons=rear;controls.update(p);
        check(controls.frame_state().look_back&&!controls.frame_state().camera,"R3 requests held rear view without cycling");
        controls.update(p);
        check(controls.frame_state().look_back,"R3 remains active while held");
        p.pads[0].connected=false;controls.update(p);
        check(!controls.frame_state().look_back,"disconnect releases rear view");

        RaceSession normal(course),rear_session(course);
        check(normal.start(error)&&rear_session.start(error),"rear camera sessions start");
        unsigned tick=0;
        const auto advance_pair=[&](std::uint32_t buttons,bool rear_held,int axis=0) {
            auto n=race_input_frame({1,buttons,axis},tick++),r=n;
            if(rear_held)r.pads[0].buttons|=rear;
            check(normal.advance(n,error)&&rear_session.advance(r,error),"rear camera pair advances");
            check(normal.car().position.x==rear_session.car().position.x&&
                  normal.car().position.y==rear_session.car().position.y&&
                  normal.car().position.z==rear_session.car().position.z,
                  "rear camera does not change vehicle motion");
        };
        advance_pair(0,false);
        while(race_car_held(normal.timing())&&tick<1000)advance_pair(0,false);
        for(int moving=0;moving<120;++moving)advance_pair(gas,false);
        for(int pov=0;pov<3;++pov) {
            for(int hold=0;hold<12;++hold) {
                advance_pair(gas,true,-6000);
                const auto subject=chase_subject(rear_session.car(),rear_session.spec());
                const auto& r=rear_session.camera();const auto& n=normal.camera();
                check(r.pov==n.pov,"R3 preserves selected POV");
                check((r.eye.x-subject.origin.x)*subject.forward.x+
                      (r.eye.y-subject.origin.y)*subject.forward.y>0,
                      "held rear view is ahead of the car");
                check(r.forward.x*subject.forward.x+r.forward.y*subject.forward.y<0,
                      "held rear view looks backward toward the front of the car");
            }
            advance_pair(0,false);
            const auto& n=normal.camera();const auto& r=rear_session.camera();
            check(r.eye.x==n.eye.x&&r.eye.y==n.eye.y&&r.eye.z==n.eye.z&&
                  r.heading==n.heading&&r.pitch==n.pitch&&r.pov==n.pov,
                  "release restores current ordinary view without orbit or stale history");
            advance_pair(gamepad_button_bit(GamepadButton::face_north),false);
            advance_pair(0,false);
        }
    }
    RaceCameraWorld camera(course.world);
    check(camera.clear_fraction({32,48,20},{32,48,0})<1,"camera segment hits synthetic road");
    check(camera.clear_fraction({32,48,20},{33,48,20})==1,"camera segment clear above road");

    check(camera.clear_fraction({32,48,20},{5032,48,20})==0,"refused long camera segment blocks camera");

    VehicleTrackQuad camera_wall;
    const float corners[4][3] = {{-3,-3,-2},{-3,3,-2},{-3,3,4},{-3,-3,4}};
    for (unsigned i = 0; i < 4; ++i)
        std::copy_n(corners[i], 3, camera_wall.corner[i]);
    const auto camera_wall_image = build_vehicle_track_collision_image({camera_wall}, error);
    const auto camera_wall_world = build_track_contact_world(
        parse_track_collision(camera_wall_image.data(), camera_wall_image.size(), 0));
    check(camera_wall_world.valid, "camera wall fixture builds");
    RaceCameraWorld wall_camera(camera_wall_world);
    const VehicleVec3 actor{.2, 0, .26};
    const auto behind_wall = wall_camera.clear_race_eye(actor, {-4.7, 0, 1.7});
    const auto near_wall = wall_camera.clear_race_eye(actor, {-2.9, 0, 1.7});
    const auto past_edge = wall_camera.clear_race_eye(actor, {-2.9, 6, 1.7});
    check(std::abs(behind_wall.x + 2.3) < 2e-5 && std::abs(behind_wall.z - 1.19758875) < 2e-5,
          "4838E0 eye clips to wall and gains native normal clearance");
    check(std::abs(near_wall.x + 2.3) < 2e-5 && std::abs(near_wall.z - 1.7) < 2e-5,
          "4838E0 applies clearance even when the eye segment is unblocked");
    check(std::abs(past_edge.x + 2.9) < 2e-5 && std::abs(past_edge.y - 6.) < 2e-5,
          "4838E0 only admits the projection inside the polygon");

    ChaseCamera wall_chase;
    wall_chase.use_retail_race_mover();
    ChaseCameraSubject wall_subject;
    wall_subject.centre_of_mass = actor;
    ChaseCameraView wall_view;
    check(wall_chase.set_pov(1) && wall_chase.reset(wall_subject, wall_camera, wall_view) &&
          std::abs(wall_view.forward.x - .99733877) < 2e-5 &&
          std::abs(wall_view.forward.z + .07290658) < 2e-5,
          "483AD0 preserves actor framing when wall clearance changes the eye");

    // Host route-driver steering is sampled every six frames; pedals must
    // still release the service brake immediately when actual speed falls.
    auto fast_course=course;fast_course.start.speed=20;
    RaceSession fast_driver(fast_course),slow_driver(course);
    check(fast_driver.start(error)&&slow_driver.start(error),"route pedal witnesses start");
    while(race_car_held(fast_driver.timing()))check(fast_driver.advance({},error),"fast countdown");
    while(race_car_held(slow_driver.timing()))check(slow_driver.advance({},error),"slow countdown");
    RaceRouteDriver pedal_driver;
    const auto braking_pad=pedal_driver.sample(fast_driver);
    const auto released_pad=pedal_driver.sample(slow_driver);
    check((braking_pad.pads[0].buttons&gamepad_button_bit(GamepadButton::trigger_left))!=0,
          "host route driver brakes above target");
    check(released_pad.pads[0].axes[0]==braking_pad.pads[0].axes[0] &&
          (released_pad.pads[0].buttons&gamepad_button_bit(GamepadButton::trigger_left))==0 &&
          (released_pad.pads[0].buttons&gas)!=0,
          "host route driver releases brake below target before next steering sample");

    // Drive a complete synthetic race through controls and collision. No
    // fabricated route events: catch lost lap/finish forwarding in the glue.
    VehicleTrackCourse two_laps=course;
    check(build_race_route(two_laps.track,2,0,0,two_laps.route,error),"two-lap synthetic route");
    RaceSession race(two_laps); RaceRouteDriver driver; bool saw_lap=false;
    check(race.start(error),"synthetic race starts");
    while (race.timing().outcome==RaceOutcome::running && race.timing().step<12000) {
        if (!race.advance(driver.sample(race),error)) { check(false,"synthetic race advances"); break; }
        const auto& result=race.timing().racers[0];
        if (!saw_lap && result.highest_lap==1) {
            saw_lap=true;
            check(result.lap_ticks[1]>0 && race.timing().outcome==RaceOutcome::running,"first lap does not finish two-lap race");
            const auto hud=race_session_hud(race);
            check(hud[0]=="EVENT 1  LAP 2 / 2","HUD advances lap");
            check(hud[1]=="CURRENT "+format_race_ticks(race.timing().step_ticks-result.crossing_ticks[0])+
                  "  LAST "+format_race_ticks(result.lap_ticks[1]),"HUD starts next lap immediately and preserves last lap");
        }
    }
    check(saw_lap,"lap event reaches integration");
    check(race.timing().outcome==RaceOutcome::finished,"integrated synthetic race finishes");
    if (race.timing().outcome==RaceOutcome::finished) {
        const auto before=race.record(); const auto step=race.timing().step;
        const auto pose=race.car().position; const auto hud=race_session_hud(race);
        const auto& result=race.timing().racers[0];
        check(result.highest_lap==2 && result.lap_ticks[2]>0,"completed lap reaches timing");
        check(hud[1]=="CURRENT "+format_race_ticks(result.lap_ticks[2])+"  LAST "+format_race_ticks(result.lap_ticks[2]),
              "finished HUD uses completed lap time");
        check(hud[2]=="FINISHED "+format_race_ticks(result.finish_ticks),"finish reaches HUD");
        check(!race.advance(race_input_frame({1,gas,32767},step),error),"advance after finish refused");
        check(race.timing().step==step && race.record()==before && race.car().position.x==pose.x &&
              race.car().position.y==pose.y && race_session_hud(race)==hud,"finish freezes physics timing record and HUD");
    }

    {
        RaceSession session(course);
        check(session.start(error)&&RaceWorldContactProbe::queued_scenery(session,error,true),
              "original pool reclamation keeps the33rd placed object collidable");
    }

    Frame track,car;
    RenderOptions options; options.width=8; options.height=8;
    RenderLimits limits; RenderStats stats;
    const auto triangle=[&](Frame& frame,double z,unsigned char red) {
        check(frame.reset(8,8,options.background,limits,error),"composite frame reset");
        ClipVertex vertices[3];
        vertices[0].x=-1; vertices[0].y=-1;
        vertices[1].x=3; vertices[1].y=-1;
        vertices[2].x=-1; vertices[2].y=3;
        for (auto& vertex : vertices) { vertex.w=1; vertex.z=z; }
        RasterMaterial material; material.fallback_pattern=false; material.fallback[0]=red;
        Rasterizer raster(frame,options,limits,stats);
        check(raster.draw_triangle(vertices,material,0,error),"composite test triangle draws");
    };
    std::vector<unsigned char> pixels;
    triangle(track,0.5,10); triangle(car,0.7,20);
    check(compose_race_frame(track,car,pixels) && pixels[0]==10,"track occludes farther car");
    triangle(car,0.3,20);
    check(compose_race_frame(track,car,pixels) && pixels[0]==20,"nearer car occludes track");
    triangle(car,0.5,30);
    check(compose_race_frame(track,car,pixels) && pixels[0]==30,"car wins exact depth tie");
    check(car.reset(8,8,options.background,limits,error),"empty car frame");
    check(compose_race_frame(track,car,pixels) && pixels[0]==10,"empty pixels never hide track");
    Frame wrong;
    check(!compose_race_frame(track,wrong,pixels),"different frame size refused");
    std::cout << "race session tests: " << failures << " failures\n";
    return failures ? 1 : 0;
}
