#include "nfsu/vehicle_track.h"
#include "nfsu/race_contacts.h"
#include "nfsu/player_powertrain.h"
#include <cmath>
#include <iostream>
#include <limits>
using namespace nfsu;
namespace {
unsigned failures=0;
void check(bool value,const char* label) {if(!value){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
}
int main() {
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
    VehicleInput input;input.throttle=.5;input.road_steering=true;input.road_angle_word_valid=true;
    VehicleTrackTelemetry telemetry;
    VehicleTrackSession car(model,course);
    {
        VehicleTrackSession follower(model,course);
        VehicleState moving;
        auto pose=course.start;
        pose.heading=0;
        pose.speed=25;
        const bool prepared=model.reset(moving,pose);
        moving.velocity.y=3;
        moving.owner_speeds.forward=25;
        check(prepared&&follower.configure_ai(1)&&follower.start(moving),"path motion setup");
        follower.select_rival_physics(4);
        AiDriverPath path;
        path.count=5;
        for(unsigned i=0;i<path.count;++i)path.points[i]={
            float(moving.position.x)+5.F*float(i),float(moving.position.y)-.5F,float(moving.position.z)};
        auto path_input=input;
        path_input.world_frame_seconds=.01F;
        check(follower.step_rival_path(path,25.F,0.F,true,path_input,telemetry)&&
              telemetry.position.y>moving.position.y&&telemetry.velocity.y>2.F,
              "path publication preserves the lateral velocity of the incoming body");
        const auto prior=follower.state();
        auto invalid_path=path;
        invalid_path.points[1][0]=std::numeric_limits<float>::infinity();
        check(!follower.step_rival_path(invalid_path,25.F,0.F,true,path_input,telemetry)&&
              follower.state().step==prior.step&&follower.state().position.y==prior.position.y,
              "invalid path publication leaves the physical state unchanged");
        AiDriverPath crossing;
        crossing.count=5;
        for(unsigned i=0;i<crossing.count;++i)crossing.points[i]={
            float(prior.position.x),float(prior.position.y)+5.F*float(i),float(prior.position.z)};
        check(!follower.step_rival_path(crossing,25.F,0.F,true,path_input,telemetry)&&
              follower.state().position.x==prior.position.x&&follower.state().step==prior.step&&
              follower.state().rigid_body.angular_momentum==prior.rigid_body.angular_momentum,
              "crossing path cannot publish a discontinuous body heading before contact");
    }
    check(car.configure_ai(1)&&car.start(course.start),"AI world starts");
    check(car.step_ai(input,telemetry),"AI first frame");
    const auto before=car.state();
    auto invalid=input;invalid.throttle=std::numeric_limits<double>::quiet_NaN();
    check(!car.step_ai(invalid,telemetry)&&car.state().step==before.step&&
          car.state().position.x==before.position.x,"invalid AI input leaves frame unchanged");
    auto timed=input;timed.world_clock_valid=true;timed.world_clock=10000;
    car.request_reset();
    check(car.step_ai(timed,telemetry)&&telemetry.reset==VehicleTrackReset::manual,
          "AI manual reset published on world tick");
    check(car.owner_recovery_active(10000)&&car.owner_recovery_active(17999)&&
          !car.owner_recovery_active(18000),"4280A0 strict two-second reset gate");
    const auto reset=car.state();
    VehicleTrackSession fresh(model,course);
    check(fresh.configure_ai(1)&&fresh.start(reset),"reset control session");
    check(car.step_ai(input,telemetry)&&fresh.step_ai(input,telemetry),"post-reset step");
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
    low_pose.ground.z-=.03;
    check(uneven_start.configure_ai(1)&&uneven_start.start(low_pose),"uneven AI placement starts");
    check(uneven_start.step_ai(input,telemetry)&&std::abs(uneven_start.state().velocity.z)<1&&
          uneven_start.ai_physics().ground_queries==16,
          "initial strut refresh cannot turn terrain offset into a launch impulse");

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
