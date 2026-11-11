#include "nfsu/intro_ai.h"
#include "nfsu/race_events.h"
#include "nfsu/ai_path_choice.h"
#include "nfsu/ai_recovery.h"
#include "nfsu/ai_catchup.h"
#include "nfsu/ai_powertrain.h"
#include "nfsu/ai_rival_action.h"
#include "nfsu/race_setup.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <limits>
namespace nfsu {
static_assert(kAiPathWorldActors>=4+kRaceTrafficCapacity,"AI snapshot covers the complete traffic pool");
class IntroAiPlanner::State {
    RacingLineTrack lines;
    std::vector<AiRoadEvent> events;
    std::array<bool,128> reverse{};
    std::array<AiRoadContext,4> contexts{};
    std::array<AiRoadPolicy,4> policies{};
    std::array<AiRivalActionController,3> actions_{};
    std::array<std::uint32_t,3> controller_tick_{};
    std::array<RaceRivalPlan,3> retained_plan_{};
    std::array<unsigned,3> full_updates_{},partial_updates_{},retained_updates_{};
    std::array<AiPlannedPath,4> published{};
    AiPlannedPath human_drag_path_{};
    AiRoadHorizons human_drag_horizons_{};
    std::array<AiDriverPoint,4> bounds_min{},bounds_max{};
    std::uint32_t rng=0x60000000U;
    unsigned frames=0,rival_count_=0;
    struct RailQuery {
        const RaceSession* world=nullptr;unsigned actor=0;
        const AiRoadContext* road=nullptr;
        WorldObstacleBinding binding;WorldObstacleStatus status;unsigned reads=0;
    };
    std::array<RailQuery,4> rail_{};
    static const RaceSession& rail_actor(const RailQuery& q){return q.actor==3?*q.world:q.world->rival(q.actor);}
    static bool rail_read(void* opaque,WorldObstacleStatus& out) {
        auto& q=*static_cast<RailQuery*>(opaque);const auto& actor=rail_actor(q);
        if(!q.world->world_obstacle_status(q.binding,actor.car(),actor.progress().coordinate.distance,out))return false;
        out.available=out.available&&q.road&&q.road->flags.first_obstacle>=0;
        q.status=out;++q.reads;return true;
    }
    static void rail_clear(void* opaque){auto& q=*static_cast<RailQuery*>(opaque);q.binding={};q.status={};}
    static bool rail_decide(void* opaque,const AiRoadEvent& event,bool& take) {
        auto& q=*static_cast<RailQuery*>(opaque);
        if(!q.world->bind_world_obstacle(event.obstacle_type,event.obstacle_enter,event.obstacle_leave,q.binding))return false;
        WorldObstacleStatus status;if(!rail_read(opaque,status))return false;
        take=!status.clear;return true;
    }
    bool trace_recovery=false;
    bool retail_speed_=false;
    int direction_=0;
    unsigned track_number_=1003;
    bool drag_=false;
    float catchup_weight_=0;
    bool catchup_enabled_=true;
    unsigned traffic_selector_=0;
    std::array<std::array<AiCatchupTuning,4>,3> catchup_tuning_{};
    std::array<AiCatchupState,3> catchup_{};
    AiCatchupHistory human_history_{};
    unsigned human_grade_=0,catchup_updates_=0;
    std::array<unsigned,3> rival_grades_{};
    bool human_ready_=false;
    std::uint32_t observed_tick_=std::numeric_limits<std::uint32_t>::max();
    std::array<float,3> catchup_low_{{1,1,1}},catchup_high_{{1,1,1}};
    std::array<DragLaneState,4> lanes{};
    std::array<float,3> drag_launch{};
    struct Recovery {
        std::vector<AiRecoveryContact> contacts;
        std::vector<AiStrategyContact> strategy_contacts;
    };
    std::array<Recovery,3> recovery{};
    std::array<AiRecoveryHistory,3> recovery_history_{};
    std::array<bool,3> stalled_reset_{};
    std::array<float,3> recovery_progress_{},recovery_rate_{};
    bool recovery_progress_ready_=false;
    float recovery_previous_dt_=1.F/60.F;
    std::array<int,4> selected_context{};
    std::array<AiSpeedConfiguration,4> speed_config{};
    std::array<AiSpeedPolicyState,3> speed_state{};
    static std::uint32_t u32(const unsigned char* p) {
        return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
    }
public:
    bool rail_snapshot(unsigned i,IntroRailSnapshot& out)const {
        if(i>=rival_count_)return false;
        out={rail_[i].binding,rail_[i].status,actions_[i].current().kind,rail_[i].reads};return true;
    }
    std::uint32_t* world_random()noexcept{return &rng;}
    void set_recovery_trace(bool value) {trace_recovery=value;}
    unsigned evaluations=0,pairs=0,side_choices=0,fallbacks=0;
    std::string difficulty_line() const {
        std::ostringstream out;out<<std::setprecision(9)<<"AI_DIFFICULTY frames="<<frames<<" updates="<<catchup_updates_
            <<" traffic_selector="<<traffic_selector_<<" reference_weight="<<catchup_weight_<<" human_history="<<human_history_.average<<" history_samples="<<human_history_.count;
        for(unsigned i=0;i<rival_count_;++i)out<<" rival"<<i<<"_context="<<selected_context[i]<<" category="<<rival_grades_[i]<<" action="<<actions_[i].current().kind<<" scale="<<catchup_[i].speed_scale
            <<" min="<<catchup_low_[i]<<" max="<<catchup_high_[i]<<" physical="<<catchup_[i].physical_scale
            <<" full="<<full_updates_[i]<<" partial="<<partial_updates_[i]<<" retained="<<retained_updates_[i];
        return out.str();
    }
    std::array<float,3> minimum_speed{{1e9F,1e9F,1e9F}},maximum_speed{};
    bool load(const std::filesystem::path& root,const std::array<int,3>& selected,
              const std::array<const VehicleHull*,4>& hulls,bool retail_speed,unsigned track_number,int direction,bool drag,float catchup_weight,int laps,bool point_to_point,unsigned traffic_selector,bool catchup_enabled) {
        if((direction!=0 && direction!=1)||traffic_selector>3)return false;
        catchup_enabled_=catchup_enabled;
        track_number_=track_number;traffic_selector_=traffic_selector;direction_=direction;retail_speed_=retail_speed;drag_=drag;catchup_weight_=catchup_weight;
        if(!std::isfinite(catchup_weight_))return false;
        std::array<unsigned char,0x82c> configuration{};
        if(retail_speed_) {
            std::ifstream file(root/"Global/GLOBALB.BUN",std::ios::binary);
            if(!file)return false;
            bool found=false;unsigned char header[8];
            while(file.read(reinterpret_cast<char*>(header),8)) {
                const auto tag=u32(header),size=u32(header+4);
                if(tag==0x34b00) {
                    if(found||size!=configuration.size()||!file.read(reinterpret_cast<char*>(configuration.data()),size))return false;
                    found=true;
                }else file.seekg(size,std::ios::cur);
                if(!file)return false;
            }
            if(!found)return false;
        }
        std::vector<unsigned char> image;
        const auto track=load_race_route_track(root/"Tracks"/track_bundle_file_name(static_cast<std::int16_t>(track_number)),image);
        if(!track.ok()||!parse_racing_line_image(image.data(),image.size(),{},lines))return false;
        bool found=false;
        for(std::size_t pos=0;pos+8<=image.size();) {
            const auto id=u32(image.data()+pos),size=u32(image.data()+pos+4);pos+=8;
            if(size>image.size()-pos)return false;
            if(id==0x34122U){if(found||!ai_road_events(image.data()+pos,size,events))return false;found=true;}
            pos+=size;
        }
        if(!found)return false;
        std::string projection_error;
        if(!ai_road_initialize(track,lines,events,projection_error)) {
            std::cerr<<projection_error<<'\n';return false;
        }
        if(!ai_road_orient(lines,direction_))return false;
        // Keep file-order events and matching file-order point references.
        for(const auto& r:lines.routes)reverse[unsigned(r.index)]=r.reverse;
        for(unsigned i=0;i<4;++i) {
            if(i<3&&!hulls[i])continue;
            if(i<3){if(i!=rival_count_)return false;++rival_count_;}
            if(!hulls[i]||hulls[i]->points.empty())return false;
            bounds_min[i]=bounds_max[i]=hulls[i]->points.front();
            for(const auto& point:hulls[i]->points)for(unsigned k=0;k<3;++k) {
                bounds_min[i][k]=std::min(bounds_min[i][k],point[k]);
                bounds_max[i][k]=std::max(bounds_max[i][k],point[k]);
            }
            if(i<3) {
                selected_context[i]=selected[i];
                if(retail_speed_&&!ai_speed_configuration(configuration.data(),configuration.size(),selected[i],speed_config[i]))return false;
            }
        }
        for(unsigned i=0;i<rival_count_;++i)for(unsigned rank=0;rank<4;++rank)
            if(!ai_catchup_tuning(configuration.data(),configuration.size(),selected[i],laps,point_to_point,traffic_selector_,rank,catchup_tuning_[i][rank]))return false;
        if(drag_) for(unsigned i=0;i<rival_count_;++i) {
            //44B1FF..44B238 /4216F0: signed launch lead from the selected
            //difficulty row. Share the world RNG with path decisions.
            const auto sample=rng%0x7fffffffU;
            const auto x=rng^0x1d872b41U,y=(x>>5)^x;rng=(y<<27)^y^x;
            const auto& cfg=speed_config[i];
            drag_launch[i]=((float(sample)*4.656612873077393e-10F)*
                (cfg.drag_launch_max-cfg.drag_launch_min)+cfg.drag_launch_min)+.001F;
        }
        return true;
    }
    bool camera_road(AiRoadChain& road,int& category)const {
        if(!contexts[3].selected)return false;
        auto chain=contexts[3].chain;auto decisions=policies[3];auto random=rng;
        AiRoadPolicyInput policy;policy.direction=direction_;policy.role=1;
        policy.decision_start=0;policy.probabilities={0,0,1};
        if(!ai_road_extend(lines,events,reverse,contexts[3].category,100,policy,decisions,random,chain))return false;
        road=chain;category=contexts[3].category;return true;
    }
    bool drag_human_snapshot(IntroDragSnapshot& out)const {
        if(!drag_||!contexts[3].selected)return false;
        out.lane=lanes[3];out.road=contexts[3];out.horizons=human_drag_horizons_;
        out.action=human_drag_path_;return true;
    }
    bool observe_human(const RaceSession& race) {
        const auto tick=static_cast<std::uint32_t>(race.timing().now_ticks);
        if(observed_tick_==tick)return true;
        if(!human_ready_) {
            float rating=0,unused=0;
            if(!ai_setup_performance(race.spec().handling,rating,unused))return false;
            human_grade_=race_performance_category(rating);contexts[3].category=int(human_grade_);
            //450210 reads each ride's own424, independently of difficulty735.
            //Both authored lane and speed columns use this category.
            for(unsigned i=0;i<rival_count_;++i) {
                if(!ai_setup_performance(race.rival(i).spec().handling,rating,unused))return false;
                rival_grades_[i]=race_performance_category(rating);
                contexts[i].category=int(rival_grades_[i]);
            }
            human_ready_=true;
        }
        //430758 also samples during the countdown. It is independent of
        //whether the rivals' driving/physics is held, and never consumes RNG.
        bool found=false;float authored_speed=0;bool enabled=false;
        const float dt=race.next_world_frame_seconds();
        for(unsigned i=0;i<rival_count_;++i) {
            const float progress=race.competitive_progress(i+1);
            if(recovery_progress_ready_&&!ai_recovery_progress_rate(progress,recovery_progress_[i],
                recovery_previous_dt_,recovery_rate_[i]))return false;
            recovery_progress_[i]=progress;
            stalled_reset_[i]=false;
            const auto& action=actions_[i].current();
            if(race.timing().stage!=RaceStage::racing||!action.handle||action.kind==9||
                race.timing().racers[i+1].status!=RacerStatus::not_finished)continue;
            auto& history=recovery_history_[i];
            const auto previous_sample=history.cursor;
            if(!ai_recovery_history_step(dt,recovery_rate_[i],race.course().route.spline.path_length,history))return false;
            // 44B40C checks the reset only after writing a fresh sample.
            if(history.cursor!=previous_sample&&history.count==5&&history.average<.2F&&!race.actor_recovering(i+1)) {
                const auto& rival=race.rival(i).car().position;
                const auto& human=race.car().position;
                const float x=float(rival.x)-float(human.x),y=float(rival.y)-float(human.y),z=float(rival.z)-float(human.z);
                stalled_reset_[i]=std::sqrt((x*x+y*y)+z*z)>=50.F;
            }
        }
        recovery_progress_ready_=true;recovery_previous_dt_=dt;
        const int decrement=static_cast<int>(dt*4000.F+.5F);
        if(human_history_.timer<decrement) {
            AiRoadSelectionInput input;const auto& state=race.car();
            input.position={float(state.position.x),float(state.position.y),float(state.position.z)};
            input.progress=race.progress().coordinate.distance;
            if(direction_)input.progress=race.course().route.spline.path_length-input.progress;
            input.maximum_distance=999999.F;input.allow_outside_width=true;input.role=1;
            AiRoadCursor point;
            if(!ai_road_select(lines,input,found,point))return false;
            if(found) {
                const auto* road=lines.find(point.route);
                if(!road||point.point>=road->points.size())return false;
                authored_speed=float(road->points[point.point].speed[human_grade_]);
                enabled=road->category_enabled[human_grade_];
            }
        }
        const auto speed=race.car().owner_speeds.magnitude;
        if(!ai_catchup_history_step(dt,speed,authored_speed,enabled,found,human_history_))return false;
        observed_tick_=tick;return true;
    }
    bool plan(const RaceSession& race,RaceRivalPlans& batch,int held_lane) {
        const bool solo_finish=race.is_drift()&&race.timing().outcome==RaceOutcome::finished;
        if((!rival_count_&&!drag_&&!solo_finish)||race.rival_count()!=rival_count_||!observe_human(race))return false;
        AiPathCollisionWorld world;world.count=4;world.tick=std::uint32_t(race.timing().now_ticks);
        world.retain_parameters={16,0,0};world.prediction_horizon=.75F;
        world.normal_scale=world.human_scale=world.slow_scale=1;world.slow_speed=2;
        std::array<AiNeighborActor,kAiPathWorldActors> actors;
        std::array<AiActionPathInput,4> path_inputs;
        std::array<AiRoadLocation,4> locations;
        std::array<bool,4> road_resets{};
        const auto& timing=race.timing();
        const float seconds=timing.race_start_valid?float(timing.now_ticks-timing.race_start_ticks)*.00025F:-1.F;
        for(unsigned i=0;i<4;++i) {
            if(i<3&&i>=rival_count_){actors[i].enabled=false;continue;}
            const auto& session=i==3?race:race.rival(i);const auto& s=session.car();const auto& q=s.orientation;
            auto& a=world.actors[i];a.id=i+1;a.role=i==3?1:2;
            a.selector=drag_?1:3;
            if(i<3)a.action=actions_[i].current().handle?actions_[i].current().kind:drag_?8:7;
            a.position={float(s.position.x),float(s.position.y),float(s.position.z)};
            a.velocity={float(s.velocity.x),float(s.velocity.y),float(s.velocity.z)};
            //4576B0 reads published Actor+80/+90/+3D0, not a new quaternion
            //projection or an independently reconstructed 60Hz acceleration.
            a.forward=s.rigid_body_ready?s.owner_motion.axes[0]:AiDriverPoint{{float(1-2*(q.y*q.y+q.z*q.z)),float(2*(q.x*q.y+q.w*q.z)),float(2*(q.x*q.z-q.w*q.y))}};
            a.heading=s.rigid_body_ready?s.owner_motion.heading:track_angle(a.forward[0],a.forward[1]);
            a.absolute_speed=s.owner_speeds.magnitude;a.signed_speed=s.owner_speeds.forward;
            a.acceleration=s.owner_motion.acceleration;
            a.bound_min=bounds_min[i];a.bound_max=bounds_max[i];
            auto& n=actors[i];n.id=a.id;n.position=a.position;n.velocity=a.velocity;n.forward=a.forward;
            //57A300 excludes permanent ghost actors from the live neighbor
            //registry. Keep its own stop path, but do not avoid a retired car.
            n.enabled=!race.timing().racers[i==3?0:i+1].permanent_ghost;
            n.progress=session.progress().coordinate.distance;
            n.bounds_min={a.bound_min[0],a.bound_min[1]};n.bounds_max={a.bound_max[0],a.bound_max[1]};
            a.path=published[i];
            //57A530 publishes before rival controllers: ordinary human owner1
            //uses fresh body motion, not its road-following Driver path. Keep
            //owner2 drag and the owner3 finish controller on their own paths.
            if(i==3&&!drag_&&timing.outcome==RaceOutcome::running) {
                const auto& forward=s.rigid_body_ready?s.rigid_body.axes[0]:a.forward;
                if(!ai_human_world_prediction(a.position,a.velocity,forward,s.owner_speeds.magnitude,a.path))return false;
            }
            if(!ai_path_cursor(a.path,a.signed_speed,a.position,a.velocity,world.tick,16,0,0))return false;
        }
        for(unsigned i=0;i<4;++i){rail_[i].world=&race;rail_[i].actor=i;rail_[i].road=&contexts[i];}
        const auto prepare_context=[&](unsigned i) {
            auto& a=world.actors[i];auto& n=actors[i];
            const float raw_progress=direction_?race.course().route.spline.path_length-n.progress:n.progress;
            AiRoadContextInput input;input.track_number=int(track_number_);input.selection.position=a.position;input.selection.progress=raw_progress;
            // Geometry and enabled lanes are already direction-oriented.
            input.selection.direction=0;
            input.selection.require_enabled_lane=drag_;
            input.selection.maximum_distance=999999;input.selection.allow_outside_width=true;
            input.body_width=a.bound_max[1]-a.bound_min[1];input.back_margin=24;
            input.controller_kind=i==3?(drag_?2:1):3;
            //5A5730 compact slot0 uses RideInfo424, independently of the
            //selected11..16 lane. The six other slots carry lane bounds.
            input.ride_category=int(i==3?human_grade_:rival_grades_[i]);
            input.initial_horizon=input.retained_horizon=60;
            if(i<3||(drag_&&i==3)) {
                AiRoadHorizons horizons;
                if(!ai_road_horizons(a.absolute_speed,2.F,3.F,horizons))return false;
                input.initial_horizon=horizons.initial;input.retained_horizon=horizons.retained;
                if(i==3)human_drag_horizons_=horizons;
            }
            AiRoadPolicyInput policy;policy.direction=direction_;policy.role=i==3?1:2;
            policy.decision_start=0;policy.race_seconds=seconds;policy.probabilities={0,0,1};
            policy.obstacle_context=&rail_[i];policy.obstacle_decision=rail_decide;
            if(retail_speed_&&i<3)policy.probabilities=speed_config[i].road_probabilities;
            AiRoadContextResult result;
            //458CC0 before new context decisions; current retail6B753C=10.
            // Without expiry the eighth consumed junction exhausts the cache.
            if(!ai_road_expire(events,raw_progress,race.course().route.spline.path_length,!race.course().route.point_to_point,!direction_,10,policies[i]))return false;
            if(!frames&&i<rival_count_) {
                const auto start_road=race.rival_start_road(i);
                if(start_road>=0) {
                    const auto* route=lines.find(start_road);
                    if(!route||route->points.empty())return false;
                    // 435D30 asks 442690 for the closest point on the authored
                    // road with neither heading nor traffic permission tests.
                    const auto distance=[&](const RoutePoint& p) {
                        const float x=a.position[0]-p.position[0],y=a.position[1]-p.position[1];
                        return x*x+y*y;
                    };
                    const auto point=std::min_element(route->points.begin(),route->points.end(),
                        [&](const RoutePoint& x,const RoutePoint& y){return distance(x)<distance(y);});
                    contexts[i].road={start_road,std::uint16_t(point-route->points.begin())};
                    contexts[i].selected=true;
                }
            }
            bool human_drag_rebuild=!frames;
            bool human_bootstrap=false;
            if(drag_&&i==3) {
                auto& context=contexts[i];
                // The retail controller is initialized before accepting input.
                // This adapter creates its first road lazily on the first plan.
                if(!context.selected||!context.chain.count) {
                    if(!ai_road_update_context(lines,events,reverse,input,policy,policies[i],rng,context,result)||!context.selected)return false;
                    const auto* road=lines.find(context.road.route);if(!road)return false;
                    const int initial=ai_road_nearest_lane(*road,context.road.point,a.position);
                    if(initial<0)return false;
                    lanes[i].category=context.category=initial;
                    human_bootstrap=human_drag_rebuild=true;
                }
                const auto prior_lane=lanes[i];
                const auto* road=lines.find(context.road.route);if(!road)return false;
                //4605E0/45D800 select on the RETAINED road, then45D908 calls
                //462430/44CF30. Do not reapply manual selection after common
                //admission: that would undo its unavailable-lane escape.
                if(!drag_lane_step(*road,context.road.point,a.position,context.chain,
                    context.flags.enabled,held_lane,race.travelled_distance(),lanes[i],
                    {int(track_number_),direction_,raw_progress,2}))return false;
                human_drag_rebuild=human_drag_rebuild||prior_lane.mode!=lanes[i].mode||
                    context.category!=lanes[i].category;
                context.category=lanes[i].category;
            }
            if(!ai_road_update_context(lines,events,reverse,input,policy,policies[i],rng,contexts[i],result)||
               !contexts[i].selected||!ai_road_locate_actor(contexts[i].chain,a.position,contexts[i].category,locations[i])) {
                std::cerr<<"context refused actor "<<i<<" frame "<<frames<<" progress "<<n.progress
                    <<" road "<<contexts[i].road.route<<":"<<contexts[i].road.point<<" cache";
                for(auto slot:policies[i].cache)std::cerr<<' '<<std::hex<<slot<<std::dec;
                std::cerr<<'\n';return false;
            }
            human_drag_rebuild=human_drag_rebuild||result.reselected||result.reset_maneuver;
            road_resets[i]=human_bootstrap||result.reselected||result.reset_maneuver;
            if(drag_&&i==3) {
                human_drag_rebuild=human_drag_rebuild||lanes[i].category!=contexts[i].category;
                lanes[i].category=contexts[i].category;
            }
            if(drag_&&i<3) {
                auto& context=contexts[i];const auto* road=lines.find(context.road.route);
                if(!road)return false;
                if(!frames) {
                    //4528B0/45CFF0 initializes the rival lane from its pose.
                    const int initial=ai_road_nearest_lane(*road,context.road.point,a.position);
                    if(initial<0)return false;
                    lanes[i].category=initial;
                }else lanes[i].category=context.category;
                if(context.category!=lanes[i].category) {
                    context.category=lanes[i].category;
                    if(!ai_road_refresh(lines,events,reverse,context.road,context.category,a.position,false,
                        input.initial_horizon,input.retained_horizon,policy,policies[i],rng,context.chain)||
                       !ai_road_flags(lines,events,context.chain,context.category,context.flags)||
                       !ai_road_locate_actor(context.chain,a.position,context.category,locations[i]))return false;
                }
            }
            auto& p=path_inputs[i];p.roads=contexts[i].chain.cells.data();p.road_count=contexts[i].chain.count;
            p.selected=locations[i].cell;p.initial_offset=locations[i].lateral;p.signed_speed=a.signed_speed;
            p.policy.absolute_speed=a.absolute_speed;p.policy.race_seconds=seconds;
            p.policy.special_context=drag_;
            //45C59F reads actor24 (drag controller2), NOT physics selector28
            //(selector1). Conflating them selects zero lateral progress below
            //60mph instead of the original drag5..1 per-sample curve.
            p.policy.physics_selector=i==3?(drag_?2:1):3;
            p.extension.category=contexts[i].category;p.extension.actor_position=a.position;p.extension.actor_velocity=a.velocity;
            p.extension.actor_forward=a.forward;p.extension.absolute_speed=a.absolute_speed;
            p.extension.clearance=input.body_width*.5F;p.extension.margin=1;
            p.tick=world.tick;p.retain_parameters=world.retain_parameters;
            p.previous.path=published[i];
            p.rebuild=i==3||ai_strategy_rebuild_path(drag_?AiStrategyKind::drag:AiStrategyKind::ordinary,
                result.reselected||!published[i].path.count);
            p.category_available=ai_road_category_available(lines,contexts[i].road,contexts[i].category);
            if(drag_&&i==3) {
                p.rebuild=human_drag_rebuild||!human_drag_path_.path.count;
                p.previous.path=human_drag_path_;
            }
            if(!frames||i==3) {
                AiManeuverResult seed;
                if(!ai_action_plan_path({},p,seed))return false;
                if(drag_&&i==3) {
                    //45FAF0 copies the controller path before459980 resamples
                    //the driver's copy. Never feed that copy back into retention.
                    human_drag_path_=seed.path;
                    if(!ai_path_publish(seed.path))return false;
                }
                published[i]=seed.path;
            }
            a.path=published[i];
            // Refresh target path cursor against this same physical snapshot.
            if(!ai_path_cursor(a.path,a.signed_speed,a.position,a.velocity,world.tick,16,0,0))return false;
            return true;
        };
        if(race.traffic()) {
            const auto& traffic=race.traffic()->actors();
            if(4+traffic.size()>world.actors.size()){std::cerr<<"traffic exceeds AI snapshot capacity\n";return false;}
            world.count=4+traffic.size();
            for(unsigned i=0;i<traffic.size();++i) {
                const auto& t=traffic[i];const auto& s=t.car;const auto& q=s.orientation;
                auto& a=world.actors[4+i];auto& n=actors[4+i];a.id=n.id=5+i;n.enabled=t.active;
                a.role=3;a.selector=t.impacted?1:2;
                a.position={float(s.position.x),float(s.position.y),float(s.position.z)};
                a.velocity={float(s.velocity.x),float(s.velocity.y),float(s.velocity.z)};
                a.forward=s.rigid_body_ready?s.rigid_body.axes[0]:AiDriverPoint{{float(1-2*(q.y*q.y+q.z*q.z)),float(2*(q.x*q.y+q.w*q.z)),float(2*(q.x*q.z-q.w*q.y))}};
                a.heading=track_angle(a.forward[0],a.forward[1]);
                a.absolute_speed=s.owner_speeds.magnitude;
                a.signed_speed=s.owner_speeds.forward;
                a.bound_min={float(t.box.low.x),float(t.box.low.y),float(t.box.low.z)};
                a.bound_max={float(t.box.high.x),float(t.box.high.y),float(t.box.high.z)};
                if(t.active&&!race.traffic()->world_prediction(i,a,world.tick,world.prediction_horizon,a.path))return false;
                a.traffic_prediction_ready=t.active;
                n.position=a.position;n.velocity=a.velocity;n.forward=a.forward;
                // Neighbor queries use race-direction spline coordinates;
                // traffic road records retain authored forward distances.
                n.progress=direction_?race.course().route.spline.path_length-t.progress:t.progress;
                n.bounds_min={a.bound_min[0],a.bound_min[1]};n.bounds_max={a.bound_max[0],a.bound_max[1]};
            }
        }
        std::array<AiPathPlannerInput,3> inputs;
        std::array<AiNeighborhood,3> neighborhoods;
        std::array<std::array<unsigned,16>,3> eligible{};std::array<unsigned,3> counts{};
        const auto prepare_input=[&](unsigned i) {
            const auto& a=world.actors[i];auto& in=inputs[i];in.path=path_inputs[i];
            in.path.previous.path=published[i];
            in.choice.current=actions_[i].maneuver();
            in.choice.current_available=in.path.category_available;
            //7006EC is the maneuver horizon2, not projection6B7538=.75.
            in.choice.horizon=2.F;in.side_score_scale=1.2000000476837158F;in.fallback_margin=1;
            in.fitness={a.position,a.velocity,a.signed_speed,a.absolute_speed,world.tick,{16,0,0},.4F,.5F,30,255};
            AiNeighborInput ni;ni.self_id=a.id;ni.position=a.position;ni.velocity=a.velocity;ni.forward=a.forward;
            ni.acceleration=a.acceleration;ni.heading=a.heading;ni.progress=actors[i].progress;
            AiRoadHorizons horizons;if(!ai_road_horizons(a.absolute_speed,2.F,3.F,horizons))return false;
            ni.horizon=horizons.retained;ni.track_length=race.course().route.spline.path_length;ni.category=contexts[i].category;ni.ride_category=int(rival_grades_[i]);ni.circuit=!race.course().route.point_to_point;
            auto& neighbors=neighborhoods[i];
            if(!ai_neighbors(contexts[i].chain,ni,actors.data(),world.count,neighbors)||neighbors.self_index<0)return false;
            const auto& self=neighbors.entries[unsigned(neighbors.self_index)];
            for(unsigned j=0;j<neighbors.count;++j) {
                const auto& n=neighbors.entries[j];if(n.id==a.id)continue;
                const unsigned other=n.id-1;if(other>=world.count)return false;
                bool admitted=false;unsigned sides=0;
                if(!ai_path_neighbor_policy(contexts[i].chain,neighbors,j,contexts[i].category,{other==3?-.5F:0.F,5,1,2,50,5,.11999999731779099F},admitted,sides))return false;
                const auto& b=world.actors[other];
                in.choice.neighbors[in.choice.count++]={n.id,admitted,sides,b.velocity[1]*a.velocity[1]+b.velocity[0]*a.velocity[0]};
                if(admitted)eligible[i][counts[i]++]=other;
                if(in.target_count>=in.targets.size())return false;
                const unsigned index=in.target_count++;auto& m=in.targets[index];
                in.target_ids[index]=n.id;in.target_available[index]=in.path.category_available;
                m.self={a.position,a.velocity,self.lateral,self.half_extent[0],self.distance,int(self.cell),2,true,false};
                m.target={b.position,b.velocity,n.lateral,n.half_extent[0],n.distance,int(n.cell),b.role,true,false};
                m.policy={4,5,5,2,1.5F,5,2,.707099974155426F};m.self_signed_speed=a.signed_speed;
                m.target_path=b.path;m.target_cursor=b.path.cursor;
            }
            //45AD30: side descriptors require their target in the CURRENT
            // neighborhood. Road-category availability applies only to kind1.
            if(in.choice.current.kind==2||in.choice.current.kind==3) {
                in.choice.current_available=false;
                for(unsigned j=0;j<in.target_count;++j)
                    if(in.target_ids[j]==in.choice.current.target)in.choice.current_available=true;
            }
            return true;
        };
        {
            batch.step=race.timing().step;batch.count=rival_count_;
            batch.drag_steering=drag_;batch.drag_world_random=drag_?&rng:nullptr;
            // Original world controllers publish in actor order. A later
            // controller observes the paths already published by earlier ones.
            for(unsigned i=0;i<rival_count_;++i) {
                bool full=false;
                if(!ai_rival_full_update(std::uint32_t(race.world_visual_frame()),world.tick,
                    controller_tick_[i],i,rival_count_,actions_[i].current().handle!=0,full))return false;
                const bool newly_finished=race.timing().racers[i+1].status!=RacerStatus::not_finished&&actions_[i].current().kind!=17;
                full=full||newly_finished; // race owner installs finish action17
                const bool blocking=actions_[i].current().handle&&actions_[i].current().kind==5;
                const auto& retained_maneuver=actions_[i].maneuver();
                const bool human_side=(retained_maneuver.kind==2||retained_maneuver.kind==3)&&retained_maneuver.target==4;
                if(!full&&!blocking&&!human_side) {
                    auto& p=batch.cars[i];p=retained_plan_[i];p.path=published[i].path;
                    p.recovery_cleanup=false;
                    p.reset_car=stalled_reset_[i];
                    // 45FAC8 reads current actor3B8 even on retained-plan frames.
                    p.route_distance=race.rival(i).travelled_distance();
                    if(!p.path.count)return false;
                    ++retained_updates_[i];continue;
                }
                if(full){controller_tick_[i]=world.tick;++full_updates_[i];}
                else ++partial_updates_[i];
                if(!prepare_context(i)||!prepare_input(i))return false;
                auto view=world;view.self=i;view.eligible=eligible[i];view.eligible_count=counts[i];view.pair_queries=0;
                const auto& actor=view.actors[i];auto& context=contexts[i];
                const auto* route=lines.find(context.road.route);
                if(!route||context.road.point>=route->points.size())return false;
                const auto& road=route->points[context.road.point];auto& stored=recovery[i];
                AiRecoveryInput recover;recover.tick=std::uint32_t(race.timing().now_ticks);
                const auto start_tick=timing.race_start_valid?std::uint32_t(timing.race_start_ticks):recover.tick;
                recover.race_time_hold=ai_recovery_race_hold(recover.tick,start_tick,race.rival_actor_id(i),
                    int(track_number_),drag_);
                recover.road=&road;
                recover.supported=race.rival(i).car().published_support>=3;
                recover.minimum_x=actor.bound_min[1];recover.maximum_x=actor.bound_max[1];
                recover.motion.position={actor.position[0],actor.position[1]};
                recover.motion.prediction_vector={actor.velocity[0],actor.velocity[1]};
                recover.motion.body_direction={actor.forward[0],actor.forward[1]};
                recover.motion.heading=actor.heading;recover.motion.signed_speed=actor.signed_speed;
                recover.motion.absolute_speed=actor.absolute_speed;
                const auto& target=race.rival_driver(i).target.point;recover.motion.target={target[0],target[1]};
                stored.contacts.clear();stored.strategy_contacts.clear();
                struct ReadRecovery {
                    const AiRecoveryInput& input;const AiDriverPoint& velocity;
                    std::vector<AiRecoveryContact>& contacts;AiRecoveryContactBasis basis;bool okay=true;
                } read{recover,actor.velocity,stored.contacts,{},true};
                race.visit_actor_contacts(i+1,&read,[](void* data,const ContactRecord& record) {
                    auto& r=*static_cast<ReadRecovery*>(data);AiRecoveryContact contact;
                    contact.created_tick=record.creation_tick;
                    if(!ai_recovery_contact_local({record.point[0],record.point[1]},r.input.motion.position,
                        r.velocity,r.input.motion.signed_speed,r.input.motion.heading,r.basis,contact.local))r.okay=false;
                    else r.contacts.push_back(contact);
                });
                if(!read.okay)return false;
                race.visit_actor_pairs(i+1,&stored.strategy_contacts,[](void* data,unsigned a,unsigned b,const ContactRecord& record) {
                    auto& pairs=*static_cast<std::vector<AiStrategyContact>*>(data);
                    const auto id=[](unsigned actor){return actor==0?4U:actor<=3?actor:actor+1;};
                    pairs.push_back({record.magnitude_sum,0,0,id(a),id(b)});
                });
                recover.contacts=stored.contacts.data();recover.contact_count=stored.contacts.size();
                AiRivalActionInput frame;frame.tick=recover.tick;frame.random=&rng;
                frame.drag_strategy=drag_;frame.force_traffic_action=drag_;
                frame.wall_contact=race.driver_wall_contact(i+1).active;
                frame.nearest_lane=ai_road_nearest_lane(*route,context.road.point,actor.position);
                frame.planner=&inputs[i];frame.world=&view;frame.neighbors=&neighborhoods[i];frame.road=&context.chain;
                frame.contacts=stored.strategy_contacts.data();frame.contact_count=stored.strategy_contacts.size();
                frame.recovery=&recover;
                frame.recovery_hold_seconds=ai_recovery_hold(recover.road,recover.motion.position,recovery_history_[i]);
                frame.recovery_ahead=&ai_recovery_lookahead(*route,context.road.point);
                frame.ride_category=int(rival_grades_[i]);frame.hold=race_car_held(timing);frame.race_seconds=seconds;
                frame.road_reset=road_resets[i];
                AiBlockingMode mode;
                if(!ai_strategy_blocking_mode(race.rival_ai_mode(i),mode))return false;
                frame.mode_priority=mode.priority;frame.subtract_lengths=mode.subtract_lengths;
                frame.finished=race.timing().racers[i+1].status!=RacerStatus::not_finished;
                frame.support=race.rival(i).car().published_support;
                const auto& q=race.rival(i).car().orientation;
                frame.up={float(2*(q.x*q.z+q.w*q.y)),float(2*(q.y*q.z-q.w*q.x)),float(1-2*(q.x*q.x+q.y*q.y))};
                frame.target_probability=speed_config[i].maneuver_probability;
                frame.obstacle_context=&rail_[i];frame.obstacle_read=rail_read;frame.obstacle_cleanup=rail_clear;
                frame.obstacle_marker_present=context.flags.first_obstacle>=0;
                if(frame.obstacle_marker_present) {
                    const unsigned cell=unsigned(context.flags.first_obstacle);
                    if(cell+1<context.chain.count&&context.chain.junction[cell+1]) {
                        const auto& point=context.chain.cells[cell].point;
                        for(auto id:{point.event_a,point.event_b}) {
                            if(id==255)break;
                            if(id>=events.size())return false;
                            if(events[id].target_route==context.chain.cells[cell+1].point.route_index) {
                                frame.obstacle_marker_animation=events[id].type==9;break;
                            }
                        }
                    }
                }
                frame.traffic_continuation=context.flags.first_limited<0;
                const auto& h=race.rival(i).spec().handling;
                frame.slowdown={h.downforce_front,h.downforce_rear,h.mass,h.tyres[0].static_friction,h.tyres[1].static_friction,9.8F};
                for(unsigned j=0;j<neighborhoods[i].count;++j) {
                    const auto id=neighborhoods[i].entries[j].id;if(!id||id>view.count)return false;
                    const auto& other=view.actors[id-1];
                    //44ED70 requires actor1C (a race row); traffic has no racer row.
                    frame.actors[j]={other.role==1||other.role==2,other.role==1,other.absolute_speed};
                    frame.motion[j]={id,other.role==1||other.role==2,other.role,other.velocity,
                        other.role==3?AiStrategyKind::traffic:drag_?AiStrategyKind::drag:AiStrategyKind::ordinary};
                }
                const auto previous_kind=actions_[i].current().kind;
                AiRivalActionOutput action;
                if(!(full?actions_[i].step(frame,action):actions_[i].refresh(frame,action))) {
                    std::cerr<<"action refused frame "<<frames<<" rival "<<i<<" kind "<<actions_[i].current().kind<<'\n';return false;
                }
                if(trace_recovery&&action.action.kind!=previous_kind)
                    std::cout<<"AI_ACTION frame="<<frames<<" actor="<<i<<" kind="<<action.action.kind
                        <<" priority="<<action.action.priority<<" target="<<action.target<<" speed="<<action.speed.speed
                        <<" route_speed="<<action.speed.route_speed<<'\n';
                if(action.clear_road){context.selected=false;context.chain={};}
                if(action.lane_category>=11&&action.lane_category<=16) {
                    context.category=action.lane_category;lanes[i].category=action.lane_category;
                }
                auto publication=action.path;
                if(action.path_published&&!ai_path_publish(publication))return false;
                auto& p=batch.cars[i];p=retained_plan_[i];p.path=publication.path;
                p.driver_category=int(rival_grades_[i]);p.route_distance=race.rival(i).travelled_distance();
                p.recovery_cleanup=action.reset_driver;
                p.reset_car=action.rollover_reset||stalled_reset_[i];
                //44B707 fast human-side refresh only publishes a path. Its
                //previous desired speed, catchup scale and RNG jitter survive.
                if(full||blocking) {
                    p.enabled=action.speed.enabled;
                    const auto rank=std::max(1U,race.standings().position(0))-1U;
                    AiCatchupInput catchup;catchup.tick=frame.tick;catchup.target_id=1;
                    catchup.elapsed_ticks=race.timing().race_ticks;catchup.difficulty=selected_context[i];
                    catchup.special_context=drag_;
                    catchup.self_rank=int(std::max(1U,race.standings().position(i+1))-1U);catchup.target_rank=int(rank);
                    catchup.finished=frame.finished;catchup.action_allows=action.speed.catchup;
                    catchup.suppressed=!catchup_enabled_;
                    catchup.self_progress=race.competitive_progress(i+1);catchup.target_progress=race.competitive_progress(0);
                    catchup.route_length=race.course().route.spline.path_length;
                    catchup.self_speed=actor.absolute_speed;catchup.target_speed=world.actors[3].absolute_speed;
                    catchup.target_weight=catchup_weight_;catchup.target_history=human_history_.average;
                    const auto prior=catchup_[i].updated_tick;
                    if(!ai_catchup_update(catchup,catchup_tuning_[i][rank],catchup_[i]))return false;
                    if(prior!=catchup_[i].updated_tick)++catchup_updates_;
                    catchup_low_[i]=std::min(catchup_low_[i],catchup_[i].speed_scale);
                    catchup_high_[i]=std::max(catchup_high_[i],catchup_[i].speed_scale);
                    p.physical_scale=catchup_[i].physical_scale;
                    const auto& cfg=speed_config[i];AiSpeedPolicyInput speed;
                    speed.route=route;speed.point=context.road.point;speed.category=context.category;
                    speed.context=selected_context[i];speed.tick=world.tick;speed.offset_interval=cfg.offset_interval;
                    speed.context_scale=cfg.context_scale;speed.catchup_scale=catchup_[i].speed_scale;
                    speed.offset_amplitude=cfg.offset_amplitude;speed.strategy_cap=1000000000.F;
                    speed.route_speed=action.speed.route_speed;speed.action_speed=action.speed.speed;
                    speed.duration=action.speed.duration;speed.auxiliary=action.speed.auxiliary;speed.enabled=action.speed.enabled;
                    if(policies[i].marker>=0) {
                        const auto& marker=events.at(unsigned(policies[i].marker));const auto* marked=lines.find(marker.target_route);
                        if(!marked||marker.target_point>=marked->points.size())return false;
                        speed.marker_point=&marked->points[marker.target_point];
                    }
                    AiSpeedPublication speed_output;
                    if(retail_speed_) {
                        if(!ai_action_publish_speed(speed,speed_state[i],rng,speed_output))return false;
                        p.desired_speed=speed_output.speed;p.enabled=speed_output.enabled;
                    }else p.desired_speed=action.speed.route_speed?12.F:action.speed.speed;
                }
                minimum_speed[i]=std::min(minimum_speed[i],p.desired_speed);
                maximum_speed[i]=std::max(maximum_speed[i],p.desired_speed);
                if(!p.path.count)return false;
                published[i]=publication;world.actors[i].path=publication;world.actors[i].action=action.action.kind;
                retained_plan_[i]=p;
                evaluations+=action.evaluations;pairs+=action.pair_queries;
                side_choices+=action.maneuver.kind!=1;fallbacks+=action.action.kind==4;
            }
            //420C8A: rival controllers precede the human controller. Do not
            //consume every actor's road RNG before the first rival action.
            if(!prepare_context(3))return false;
            batch.human.path=published[3].path;batch.human.desired_speed=world.actors[3].absolute_speed;
            batch.human.route_distance=race.travelled_distance();
            batch.human.driver_category=int(human_grade_);
            if(drag_)for(unsigned i=0;i<rival_count_;++i) {
                auto& p=batch.cars[i];const auto& t=race.timing();
                if(race_car_held(t)) {
                    const float remaining=t.countdown_valid?race.timing_config().countdown_step_seconds*3.F-
                        float(t.now_ticks-t.countdown_start_ticks)*.00025F:1e9F;
                    p.grid_throttle=drag_launch[i]<0&&remaining< -drag_launch[i]?1.F:0.F;
                }else if(drag_launch[i]>=0&&float(t.now_ticks-t.go_ticks)*.00025F<drag_launch[i])p.desired_speed=0;
            }
            ++frames;return true;
        }
    }
};

IntroAiPlanner::IntroAiPlanner()=default;
IntroAiPlanner::~IntroAiPlanner()=default;
std::uint32_t* IntroAiPlanner::world_random()noexcept{return state_?state_->world_random():nullptr;}
void IntroAiPlanner::set_recovery_trace(bool value) {trace_recovery_=value;if(state_)state_->set_recovery_trace(value);}
bool IntroAiPlanner::camera_road(AiRoadChain& road,int& category)const {return state_&&state_->camera_road(road,category);}
bool IntroAiPlanner::drag_human_snapshot(IntroDragSnapshot& out)const {return state_&&state_->drag_human_snapshot(out);}
bool IntroAiPlanner::rail_snapshot(unsigned i,IntroRailSnapshot& out)const {return state_&&state_->rail_snapshot(i,out);}
bool IntroAiPlanner::observe_human(const RaceSession& race) {
    return state_&&state_->observe_human(race);
}
std::string IntroAiPlanner::difficulty_line() const {
    return state_?state_->difficulty_line():"AI_DIFFICULTY inactive";
}
bool IntroAiPlanner::load(const std::filesystem::path& root,const std::array<int,3>& contexts,
                          const std::array<const VehicleHull*,4>& hulls,bool retail_speed,unsigned track_number,int direction,bool drag,float catchup_weight,int laps,bool point_to_point,unsigned traffic_selector,bool catchup_enabled) {
    auto next=std::make_unique<State>();next->set_recovery_trace(trace_recovery_);
    if(!next->load(root,contexts,hulls,retail_speed,track_number,direction,drag,catchup_weight,laps,point_to_point,traffic_selector,catchup_enabled))return false;
    state_=std::move(next);evaluations=pairs=side_choices=fallbacks=0;
    minimum_speed={{1e9F,1e9F,1e9F}};maximum_speed={};return true;
}
bool IntroAiPlanner::plan(const RaceSession& race,RaceRivalPlans& batch,int held_lane) {
    if(!state_||!state_->plan(race,batch,held_lane))return false;
    evaluations=state_->evaluations;pairs=state_->pairs;
    side_choices=state_->side_choices;fallbacks=state_->fallbacks;
    minimum_speed=state_->minimum_speed;maximum_speed=state_->maximum_speed;
    return true;
}
}
