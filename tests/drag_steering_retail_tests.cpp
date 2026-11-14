// Full retail grid -> planner -> human driver publication, not just lane math.
#include "nfsu/intro_grid.h"
#include "nfsu/intro_ai.h"
#include "nfsu/race_run.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/arg_path.h"
#include "nfsu/player_powertrain.h"
#include "nfsu/race_hud.h"
#include "nfsu/race_result.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace nfsu;
namespace nfsu {struct RaceWorldContactProbe {
    static void travelled(RaceSession& session,float value){session.travelled_distance_=value;}
};}
void need(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
#include "drag_lane_step_native.inc"
float scalar(unsigned word){float f;std::memcpy(&f,&word,4);return f;}
void native_manual_stage(const std::filesystem::path& root) {
    std::vector<unsigned char> image;
    const auto source=load_race_route_track(root/"Tracks/TRACKB1003.lzc",image);
    RacingLineTrack roads;
    need(source.ok()&&parse_racing_line_image(image.data(),image.size(),{},roads),"native lane geometry input");
    for(const auto& row:kNativeDragLaneStep) {
        const auto* road=roads.find(row.road.route);need(road&&row.road.point<road->points.size(),"native current road");
        AiRoadChain chain;chain.count=row.count;chain.search_origin=row.origin;
        for(unsigned i=0;i<row.count;++i) {
            const auto& p=row.cells[i];const auto* r=roads.find(short(p.route));
            need(r&&p.point<r->points.size(),"native retained cell reference");
            auto& cell=chain.cells[i];cell.point=r->points[p.point];cell.point_index=std::uint16_t(p.point);
            cell.distance=scalar(p.distance);
            for(unsigned k=0;k<7;++k)cell.offset[k]=scalar(p.offset[k]);
        }
        DragLaneState lane;lane.category=row.before.category;lane.mode=row.before.mode;
        lane.exclude_first=row.before.first;lane.exclude_last=row.before.last;
        std::array<bool,7> available;available.fill(true);
        need(drag_lane_step(*road,row.road.point,row.position,chain,available,row.held,10,lane,{1003,0,0,2}),"native manual stage refused");
        need(lane.category==row.after.category&&lane.mode==row.after.mode&&
             lane.exclude_first==row.after.first&&lane.exclude_last==row.after.last,
             "manual lane state differs from actual4605E0/45D800");
    }
    std::cout<<"DRAG_NATIVE_MANUAL cases="<<std::size(kNativeDragLaneStep)<<std::endl;
}
int main(int argc,char** argv){try{
    if(argc!=4)return 2;
    const auto root=argument_path(argv[1]);const unsigned event=unsigned(std::stoul(argv[2])),round=unsigned(std::stoul(argv[3]));
    if(event==9&&round==0)native_manual_stage(root);
    std::string error;IntroGrid grid;need(load_race_grid(root,event,"CIVIC",2,grid,error,nullptr,nullptr,round),error);
    const IntroGridActor* human=nullptr;std::vector<RaceRivalSetup> rivals;
    std::array<int,3> contexts{};std::array<const VehicleHull*,4> hulls{};
    for(const auto& a:grid.actors){
        if(a.config.role==Event0Role::opponent){const auto i=rivals.size();contexts[i]=a.config.selected_context;hulls[i]=&a.hull;
            rivals.push_back({a.placement_spec,a.pose,a.hull,a.config.event_label,a.assets.car_type_name,a.config.ai_mode,a.config.config_id});}
        else{human=&a;hulls[3]=&a.hull;}
    }
    need(human&&rivals.size()==3,"retail drag grid incomplete");
    RaceRun run;need(run.set_event(event,error,round)&&run.set_car("CIVIC",error)&&run.set_difficulty(2,error),error);
    if(human->config.performance)need(run.set_performance_levels(*human->config.performance,error),error);
    need(run.set_intro_grid(human->pose,human->hull,rivals,contexts,error),error);
    TrackSlice track;need(load_track_slice(posix_file_system(),root,track,error,nullptr,nullptr,event,false,round),error);
    need(run.start(root,track,error),error);
    for(unsigned i=0;i<3;++i)
        need(run.session().rival(i).physical_selector()==1,"drag rivals use selector1 at the start");
    IntroAiPlanner planner;
    need(planner.load(root,contexts,hulls,true,unsigned(grid.setup->race.track),grid.setup->race.direction,true),"drag planner initialization failed");
    RaceRivalPlans plans;need(planner.plan(run.session(),plans,1),"drag planner refused real grid");
    std::cout<<"DRAG_PUBLICATION event="<<event<<" round="<<round<<" human_points="<<unsigned(plans.human.path.count)<<" steering="<<plans.drag_steering<<std::endl;
    need(plans.drag_steering&&plans.human.path.count>=2,"human drag path is empty at the production publication boundary");
    std::cout<<"DRAG_PATH distance="<<plans.human.route_distance<<std::endl;
    for(unsigned i=0;i<3;++i)need(plans.cars[i].path.count>=2,"rival drag path empty");
    need(plans.human.driver_category>=0&&plans.human.driver_category<=4,"human drag driver category invalid");
    {
        // Actual drag vtable462390/458B90 supplied-speed oracle. Exercise the
        // production human caller, not only the already-correct scalar helper.
        auto& session=const_cast<RaceSession&>(run.session());auto& state=const_cast<VehicleState&>(session.car());
        const auto saved=state;const auto& q=state.orientation;
        const VehicleVec3 forward{1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y+q.w*q.z),2*(q.x*q.z-q.w*q.y)};
        for(const auto& row:std::array<std::array<float,2>,5>{{{0,50},{20,60},{40,120},{60,180},{80,240}}}) {
            state.velocity={forward.x*row[0],forward.y*row[0],forward.z*row[0]};
            state.owner_speeds=player_publish_owner_speeds(state.velocity,forward);state.rigid_body_ready=false;
            need(planner.plan(session,plans,0),"human speed-dependent context failed");
            IntroDragSnapshot snapshot;need(planner.drag_human_snapshot(snapshot),"human retained context unavailable");
            need(std::abs(snapshot.horizons.retained-row[1])<.001F&&snapshot.horizons.initial==60,
                 "human drag caller differs from original retained/initial road horizon");
            need(snapshot.road.chain.count>0,"retained drag road was lost");
        }
        state=saved;
    }
    if(event==9) {
        std::vector<unsigned char> image;RacingLineTrack roads;
        const auto source=load_race_route_track(root/"Tracks"/track_bundle_file_name(std::int16_t(grid.setup->race.track)),image);
        need(source.ok()&&parse_racing_line_image(image.data(),image.size(),{},roads)&&
             ai_road_orient(roads,grid.setup->race.direction),"retained manual-stage geometry");
        IntroAiPlanner retained;
        need(retained.load(root,contexts,hulls,true,unsigned(grid.setup->race.track),grid.setup->race.direction,true),"retained planner setup");
        auto& session=const_cast<RaceSession&>(run.session());auto& state=const_cast<VehicleState&>(session.car());
        const auto saved=state;RaceWorldContactProbe::travelled(session,10);
        need(retained.plan(session,plans,0),"retained planner bootstrap");
        unsigned changed=0,sensitive=0;
        for(unsigned step=0;step<12;++step) {
            IntroDragSnapshot before,after;need(retained.drag_human_snapshot(before),"before manual context");
            need(before.road.chain.count>=3,"retained road too short for supplied moving-state case");
            const auto& point=before.road.chain.cells[std::min(step+2,before.road.chain.count-1)].point;
            const auto* next_road=roads.find(short(point.route_index));need(next_road!=nullptr,"next road");
            float boundary=0;unsigned enabled=0;
            for(unsigned k=0;k<6&&enabled<2;++k)if(next_road->lane_enabled[k]){boundary+=point.lane_offset[k];++enabled;}
            if(enabled==2)boundary*=.5F;
            state.position={point.position[0]+point.forward[1]*boundary,
                            point.position[1]-point.forward[0]*boundary,point.height};
            state.velocity={point.forward[0]*20,point.forward[1]*20,0};
            state.owner_speeds=player_publish_owner_speeds(state.velocity,{point.forward[0],point.forward[1],0});
            state.rigid_body_ready=false;
            const float heading=std::atan2(point.forward[1],point.forward[0]);
            state.orientation={std::cos(heading*.5F),0,0,std::sin(heading*.5F)};
            const int held=step%4==0?-1:step%4==2?1:0;
            const AiDriverPoint position{float(state.position.x),float(state.position.y),float(state.position.z)};
            const auto* old_road=roads.find(before.road.road.route);need(old_road!=nullptr,"old retained road");
            auto expected=before.lane;
            const AiLaneTransitionInput transition{grid.setup->race.track,grid.setup->race.direction,
                session.progress().coordinate.distance,2};
            need(drag_lane_step(*old_road,before.road.road.point,position,before.road.chain,
                 before.road.flags.enabled,held,session.travelled_distance(),expected,transition),"native-equivalent retained manual stage");
            need(retained.plan(session,plans,held)&&retained.drag_human_snapshot(after),"moving human context");
            need(after.lane.mode==expected.mode&&after.lane.exclude_first==expected.exclude_first&&
                 after.lane.exclude_last==expected.exclude_last,"production human consumed the updated road instead of its retained road");
            changed+=before.road.road.route!=after.road.road.route||before.road.road.point!=after.road.road.point;
            auto wrong=before.lane;const auto* updated=roads.find(after.road.road.route);need(updated!=nullptr,"updated road");
            need(drag_lane_step(*updated,after.road.road.point,position,after.road.chain,
                 after.road.flags.enabled,held,session.travelled_distance(),wrong,transition),"after-context comparison stage");
            sensitive+=wrong.mode!=expected.mode||wrong.exclude_first!=expected.exclude_first||wrong.exclude_last!=expected.exclude_last;
        }
        need(changed>0,"production manual-stage test never advanced the road context");
        need(sensitive>0,"production manual-stage test did not distinguish the wrong call order");
        std::cout<<"DRAG_RETAINED_STAGE changed="<<changed<<" order_witnesses="<<sensitive<<std::endl;
        state=saved;RaceWorldContactProbe::travelled(session,0);
    }
    // Supplied moving state isolates lane input from launch/shift timing.
    // Each side gets a fresh real grid and production pad -> planner -> driver.
    std::array<double,2> angles{};
    for(unsigned side=0;side<2;++side) {
        RaceRun moving;
        need(moving.set_event(event,error,round)&&moving.set_car("CIVIC",error)&&moving.set_difficulty(2,error),error);
        if(human->config.performance)need(moving.set_performance_levels(*human->config.performance,error),error);
        need(moving.set_intro_grid(human->pose,human->hull,rivals,contexts,error)&&moving.start(root,track,error),error);
        auto& session=const_cast<RaceSession&>(moving.session());
        auto& state=const_cast<VehicleState&>(session.car());const auto& q=state.orientation;
        VehicleVec3 forward{1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y+q.w*q.z),2*(q.x*q.z-q.w*q.y)};
        state.velocity={forward.x*12,forward.y*12,forward.z*12};
        state.owner_speeds=player_publish_owner_speeds(state.velocity,forward);
        state.rigid_body_ready=false;
        for(auto& wheel:state.wheels)wheel.contact=true;
        state.body_rest_state=0;
        RaceWorldContactProbe::travelled(session,10);
        auto& timing=const_cast<RaceTimingState&>(session.timing());
        timing.stage=RaceStage::racing;timing.go_valid=timing.race_start_valid=true;
        timing.go_ticks=timing.race_start_ticks=timing.now_ticks;
        GamepadFrame pad;pad.pads[0].connected=true;pad.pads[0].axes[0]=side?32767:-32767;
        need(moving.simulate(pad,error),error);
        for(unsigned i=0;i<3;++i)need(session.rival(i).physical_selector()==1,"drag rivals retain selector1 after GO");
        angles[side]=static_cast<std::int16_t>(moving.session().physics_telemetry()[0].model.steering_word);
        std::cout<<"DRAG_STEERING side="<<side<<" angle="<<angles[side]<<std::endl;
        need(moving.commit(error),error);
        need(session.travelled_distance()>10,"published actor distance did not advance");
        //Terminal policy consumes the contact latch on the next world frame;
        //it is a failure even when the same hit also sets engine heat/blown.
        auto& after=const_cast<VehicleState&>(session.car());
        after.drag_totalled=true;after.engine.blown=true;
        need(moving.simulate({},error),error);
        const auto& result=session.timing().racers[0];
        need(result.status==RacerStatus::totalled&&session.timing().outcome==RaceOutcome::finished&&
             !result.finish_valid&&result.position==4,"drag impact did not terminate as last-place Totaled");
        need(post_race_result_ready(result)&&!session.player_won(),"totalled result was rejected or awarded a win");
        auto report=post_race_report(session,{},{{"CIVIC","NEON","MIATA","CIVIC"}},0);
        need(report["Time"+report["PlayerRow"]]=="Totaled","post-race report lost Totaled result");
        need(moving.commit(error),error);
    }
    need(angles[0]!=angles[1],"left and right drag pad inputs produce identical road steering");
    if(event==9&&round==0) {
        need(!grid.traffic.empty(),"real drag traffic model set absent");
        const auto& traffic=grid.traffic.front();
        for(float closing:{10.F,200.F}) {
            auto a=run.session().car(),b=a;
            a.position={};b.position={4,.1,.1};a.orientation=b.orientation={1,0,0,0};
            a.velocity={closing*.5F,0,0};b.velocity={-closing*.5F,0,0};
            a.rigid_body_ready=b.rigid_body_ready=false;
            a.collision_state=b.collision_state=0;a.drag_totalled=b.drag_totalled=false;
            a.published_support=b.published_support=4;
            ContactRecord record;std::uint32_t seed=12345;
            RaceContactContext context{record,seed,10000};context.drag=true;context.physical_selectors={{1,3}};
            RacePairContact hit;
            need(resolve_race_pair(a,human->placement_spec,race_contact_box(human->hull),
                 b,traffic.placement_spec,race_contact_box(traffic.hull),hit,nullptr,&context),"real drag traffic contact refused");
            std::cout<<"DRAG_TRAFFIC closing="<<closing<<" applied="<<hit.applied<<" totalled="<<a.drag_totalled<<std::endl;
            need(hit.applied&&!b.drag_totalled&&a.drag_totalled==(closing==200),
                 "traffic current-hit latch/selector binding incorrect");
            if(a.drag_totalled)need(a.engine.heat==1.5F,"traffic drag failure lost heat");
        }
        RaceHudAtlas hud;need(load_race_hud_atlas(root,hud,error,false,true,false,unsigned(grid.setup->race.track)),error);
        need(load_race_drag_hud(root,"CIVIC",run.session().spec().handling,hud,error),error);
        for(unsigned char c:std::string_view("KM/H"))
            need(hud.lap_fonts[1][c].width>0&&hud.lap_fonts[1][c].advance>0,
                "metric drag speed label has all authored glyphs");
        need(hud.drag_failure_labels[0]=="Blown Engine"&&hud.drag_failure_labels[1]=="Totaled!",
             "original drag failure labels absent: ["+hud.drag_failure_labels[0]+"]["+hud.drag_failure_labels[1]+"]");
        for(const auto& label:hud.drag_failure_labels)for(unsigned char c:label)
            need(c==' '||hud.lap_fonts[1][c].width>0,"drag failure glyph absent");
        RaceHudFrame frame;append_race_over(hud,1,1280,720,frame,hud.drag_failure_labels[1]);
        need(frame.quads>1,"totalled HUD message did not draw");
    }
    std::cout<<"PASS retail drag human and rival paths published"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}}
