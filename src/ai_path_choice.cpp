#include "nfsu/ai_path_choice.h"
#include "nfsu/race_route.h"
#include <cmath>
#include <algorithm>
namespace nfsu {
namespace {
bool linear_motion(const AiDriverPoint& position,const AiDriverPoint& velocity,
    std::uint16_t heading,float length,AiPlannedPath& output) noexcept {
    for(const auto& v:{position,velocity})for(float x:v)if(!std::isfinite(x))return false;
    if(!std::isfinite(length))return false;
    const float speed=std::sqrt((velocity[0]*velocity[0]+velocity[1]*velocity[1])+velocity[2]*velocity[2]);
    if(!std::isfinite(speed))return false;
    AiDriverPoint direction{};
    if(speed>=1.F){const float inverse=1.F/speed;for(unsigned k=0;k<3;++k)direction[k]=velocity[k]*inverse;}
    else direction=ai_driver_heading(heading);
    AiPlannedPath next;next.path.count=1;next.route[0]=255;next.point[0]=65535;next.category[0]=255;
    for(unsigned k=0;k<3;++k){next.path.points[0][k]=position[k]+direction[k]*length;
        if(!std::isfinite(next.path.points[0][k]))return false;}
    output=next;return true;
}
bool motion_prediction(const AiDriverPoint& position,const AiDriverPoint& velocity,
    std::uint16_t heading,float horizon,AiPlannedPath& output) noexcept {
    const float speed=std::sqrt((velocity[0]*velocity[0]+velocity[1]*velocity[1])+velocity[2]*velocity[2]);
    if(!std::isfinite(speed))return false;
    return linear_motion(position,velocity,heading,std::max(50.F,speed*horizon),output);
}
}
bool ai_traffic_prediction(const AiDriverPoint& position,const AiDriverPoint& velocity,
    std::uint16_t heading,AiPlannedPath& output) noexcept {
    return motion_prediction(position,velocity,heading,4.F,output);
}

bool ai_traffic_world_prediction(const AiPathActorSnapshot& actor,const AiPlannedPath& driver_path,
    float outside,unsigned action,std::uint32_t tick,float horizon,AiPlannedPath& output) noexcept {
    if(!std::isfinite(outside)||!std::isfinite(actor.absolute_speed)||actor.absolute_speed<0)return false;
    if(outside>-.25F)
        return ai_motion_prediction(actor.position,actor.velocity,{},actor.heading,
                                    std::max(50.F,actor.absolute_speed*4.F),output);
    auto next=driver_path;
    if(action!=7&&(!ai_path_cursor(next,actor.signed_speed,actor.position,actor.velocity,tick,16,0,0)||
       !ai_path_predict(next,{actor.position,actor.velocity,{},next.cursor,tick,horizon})))return false;
    output=next;return true;
}

bool ai_human_world_prediction(const AiDriverPoint& position,const AiDriverPoint& velocity,
    const AiDriverPoint& forward,float published_magnitude,AiPlannedPath& output) noexcept {
    for(float x:forward)if(!std::isfinite(x))return false;
    if(!std::isfinite(published_magnitude)||published_magnitude<0)return false;
    const float length=published_magnitude*2.F;
    if(!std::isfinite(length))return false;
    return linear_motion(position,velocity,track_angle(forward[0],forward[1]),std::max(50.F,length),output);
}

bool ai_motion_prediction(const AiDriverPoint& position,const AiDriverPoint& velocity,
    const AiDriverPoint& acceleration,std::uint16_t heading,float maximum_length,
    AiPlannedPath& output) noexcept {
    for(const auto& v:{position,velocity,acceleration})for(float x:v)if(!std::isfinite(x))return false;
    if(!std::isfinite(maximum_length))return false;
    const float speed=std::sqrt(velocity[0]*velocity[0]+velocity[1]*velocity[1]);
    const float force=std::sqrt(acceleration[0]*acceleration[0]+acceleration[1]*acceleration[1]);
    if(!std::isfinite(speed)||!std::isfinite(force))return false;
    if(speed*.75F<force)return linear_motion(position,velocity,heading,maximum_length,output);
    AiPlannedPath next;float vx=velocity[0],vy=velocity[1],px=position[0],py=position[1],traveled=0;
    if(maximum_length>0)for(unsigned iteration=0;iteration<20&&next.path.count<20;++iteration) {
        vx=.1F*acceleration[0]+vx;vy=.1F*acceleration[1]+vy;
        const float x=.1F*vx+px,y=.1F*vy+py;
        if(!std::isfinite(x)||!std::isfinite(y))return false;
        const unsigned n=next.path.count;bool admitted=true;float span=0;
        if(n) {
            const auto& last=next.path.points[n-1];const float dx=x-last[0],dy=y-last[1];
            span=std::sqrt(dx*dx+dy*dy);
            if(span<1.F)admitted=false;
            else if(span<20.F&&n>=2) {
                const auto& prior=next.path.points[n-2];
                if(dx*(last[0]-prior[0])+dy*(last[1]-prior[1])<0)admitted=false;
            }
        }
        if(admitted) {
            next.path.points[n]={x,y,0};next.route[n]=255;next.point[n]=65535;next.category[n]=255;
            if(n)next.length=span+next.length;
            ++next.path.count;
        }
        const float dx=px-x,dy=py-y;traveled=std::sqrt(dx*dx+dy*dy)+traveled;
        if(!std::isfinite(traveled)||!std::isfinite(next.length))return false;
        if(vx*velocity[0]+vy*velocity[1]<0)break;
        px=x;py=y;
        if(maximum_length<=traveled)break;
    }
    output=next;return true;
}
bool ai_path_select_collision(const AiPathSelectionInput& in,AiPathPairEvaluator evaluate,
                               void* context,AiPathSelectionResult& output) noexcept {
    AiPathSelectionResult next;
    if(!in.road_selected||!in.path_count||in.path_count>20){output=next;return true;}
    if(in.count>in.ids.size()||!std::isfinite(in.absolute_speed))return false;
    for(unsigned j=0;j<in.count;++j) {
        const auto id=in.ids[j];if(!id)return false;
        ++next.visits;
        if(std::fabs(in.absolute_speed)<=.0010000000474974513F||id==in.ignored)continue;
        bool hit=false;float time=1234.F;
        if(!evaluate||!evaluate(context,j,hit,time))return false;
        ++next.queries;
        if(hit) {
            if(!std::isfinite(time))return false;
            if(time<next.time){next.time=time;next.target=id;}
        }
    }
    next.time_written=true;output=next;return true;
}
bool ai_path_collision_query(const void* p,const AiPlannedPath& candidate,std::uint32_t ignored,
    float horizon,std::uint32_t& obstacle,float& output) {
    if(!p)return false;
    const auto& world=*static_cast<const AiPathCollisionWorld*>(p);
    if(world.count>world.actors.size()||world.self>=world.count||world.eligible_count>16||candidate.path.count>20||!std::isfinite(horizon))return false;
    const auto& self=world.actors[world.self];
    if(self.role!=2||!std::isfinite(self.absolute_speed)||self.absolute_speed<0)return false;
    AiPathSelectionInput selection;selection.path_count=candidate.path.count;selection.road_selected=world.road_selected;
    selection.absolute_speed=self.absolute_speed;selection.ignored=ignored;selection.count=world.eligible_count;
    if(selection.road_selected&&selection.path_count)for(unsigned j=0;j<selection.count;++j) {
        const auto index=world.eligible[j];if(index>=world.count||index==world.self)return false;
        selection.ids[j]=world.actors[index].id;
    }
    struct PairContext {const AiPathCollisionWorld& world;const AiPlannedPath& candidate;float horizon;bool checked=false;};
    PairContext context{world,candidate,horizon};
    const auto pair=[](void* opaque,unsigned ordinal,bool& hit,float& time) noexcept {
        auto& context=*static_cast<PairContext*>(opaque);const auto& world=context.world;
        const auto& self=world.actors[world.self];const auto& candidate=context.candidate;const float horizon=context.horizon;
        const auto& other=world.actors[world.eligible[ordinal]];
        if(!context.checked) {
            for(float value:{world.prediction_horizon,world.normal_scale,world.human_scale,world.slow_scale,world.slow_speed})
                if(!std::isfinite(value))return false;
            context.checked=true;
        }
        if((other.role!=1&&other.role!=2&&!(other.role==3&&other.traffic_prediction_ready))||!other.id||other.path.path.count>20)return false;
        auto recovery=[](const AiPathActorSnapshot& a,const AiPlannedPath& path) {
            return a.signed_speed<0&&(a.action==10||a.action==14||a.action==20||!path.path.count);
        };
        const bool self_reverse=recovery(self,candidate);
        const bool other_reverse=other.role==2&&recovery(other,other.path);
        auto path=candidate,other_path=other.path;
        const auto& r=world.retain_parameters;
        if(!ai_path_cursor(path,self.signed_speed,self.position,self.velocity,world.tick,r[0],r[1],r[2])||
           !ai_path_predict(path,{self.position,self.velocity,self.acceleration,path.cursor,world.tick,world.prediction_horizon}))return false;
        //456CE0 and456D50 replace only reverse recovery predictions with a
        //straight motion path. They do not refuse another controller's query.
        //456CE0/456D50 ->57A820 consume retained Actor+3D4, not the
        //refreshed body norm. Rival462390 horizon is3; traffic583240 is4.
        if(self_reverse&&!linear_motion(self.position,self.velocity,self.heading,std::max(50.F,self.absolute_speed*3.F),path))return false;
        if(other_reverse) {
            if(!std::isfinite(other.absolute_speed)||other.absolute_speed<0)return false;
            const float horizon=other.role==3?4.F:3.F;
            if(!linear_motion(other.position,other.velocity,other.heading,std::max(50.F,other.absolute_speed*horizon),other_path))return false;
        }
        float sa=self.absolute_speed;
        const auto& v=other.velocity;float sb=std::sqrt((v[0]*v[0]+v[1]*v[1])+v[2]*v[2]);
        if(sa<=.0010000000474974513F)sa=.0010000000474974513F;
        if(sb<=.0010000000474974513F)sb=.0010000000474974513F;
        float ascale=self_reverse?1.2999999523162842F:self.selector==4?1.F:world.normal_scale;
        float bscale=other_reverse?1.2999999523162842F:other.role==1?world.human_scale:other.role==3?1.2999999523162842F:world.normal_scale;
        if(sa<world.slow_speed)ascale=world.slow_scale;
        if(sb<world.slow_speed)bscale=world.slow_scale;
        AiPredictionPath a,b;
        if(!ai_prediction_shapes(path.path,self.bound_min,self.bound_max,self.heading,ascale,a)||
           !ai_prediction_shapes(other_path.path,other.bound_min,other.bound_max,other.heading,bscale,b)||
           !ai_path_cursor(path,self.signed_speed,self.position,self.velocity,world.tick,r[0],r[1],r[2])||
           !ai_path_cursor(other_path,other.signed_speed,other.position,other.velocity,world.tick,r[0],r[1],r[2]))return false;
        const float acceleration=(self.acceleration[2]*self.forward[2]+self.acceleration[1]*self.forward[1])+self.acceleration[0]*self.forward[0];
        if(!ai_prediction_walk(a,{{self.position[0],self.position[1]},sa,path.cursor},
              b,{{other.position[0],other.position[1]},sb,other_path.cursor},acceleration,horizon,time))return false;
        ++world.pair_queries;
        hit=time<99999;return true;
    };
    AiPathSelectionResult result;
    if(!ai_path_select_collision(selection,pair,&context,result))return false;
    obstacle=result.target;if(result.time_written)output=result.time;return true;
}
bool ai_path_evaluate(const AiActionPathInput& path,const AiPathFitnessInput& fitness,
    const AiPathCollisionQuery& query,float scale,float horizon,AiPathCandidate& output) noexcept {
    if(!std::isfinite(scale)||!std::isfinite(horizon))return false;
    auto next=output;next.score=-1;next.obstacle=0;next.collision_time=99999;next.accepted=false;
    if(!ai_action_plan_path(next.descriptor,path,next.geometry))return false;
    if(next.geometry.maneuver) {
        auto input=fitness;input.transition_end=next.geometry.transition_end;
        if(!ai_path_fitness(next.geometry.path,input,next.score))return false;
        if(next.score> -1) {
            const auto ignored=next.side_admitted&&!next.geometry.clipped?next.descriptor.target:0;
            if(next.side_admitted&&!next.geometry.clipped)next.score*=scale;
            if(!std::isfinite(next.score)||!query.run||!query.run(query.context,next.geometry.path,
                ignored,horizon,next.obstacle,next.collision_time)||!std::isfinite(next.collision_time))return false;
            next.accepted=next.collision_time>horizon;
        }
    }
    output=next;return true;
}
namespace {
bool valid_score(float score){return std::fabs(score-(-1.F))>.0010000000474974513F;}
bool same(const AiContactManeuver& a,const AiContactManeuver& b) {
    if(a.kind>=2&&a.kind<=3)return a.kind==b.kind&&a.target==b.target;
    return a.kind==b.kind&&std::fabs(a.parameter-b.parameter)<=.0010000000474974513F;
}
bool side(unsigned policy,int kind){return policy==1||policy==static_cast<unsigned>(kind);}
}
bool ai_path_choose(const AiPathChoiceInput& in,const AiPathChoiceOps& ops,void* context,
                    AiPathChoiceResult& output) noexcept {
    if(in.count>16||!std::isfinite(in.horizon)||!std::isfinite(in.current.parameter)||
       !ops.evaluate||!ops.available||!ops.fallback)return false;
    for(unsigned i=0;i<in.count;++i)
        if(in.neighbors[i].sides>3||!std::isfinite(in.neighbors[i].velocity_dot))return false;
    AiPathCandidate retained,best,backup;AiPathChoiceResult result;
    retained.descriptor=in.current_available?in.current:AiContactManeuver{};
    //45B860's empty descriptor has kind0; it cannot pass ordinary admission.
    best.descriptor.kind=0;backup.descriptor.kind=0;
    std::uint32_t retained_obstacle=0;
    auto evaluate=[&](AiPathCandidate& candidate,std::uint32_t target,bool& usable) {
        if(!ops.evaluate(context,candidate)||!std::isfinite(candidate.score)||!std::isfinite(candidate.collision_time))return false;
        ++result.evaluations;
        if(candidate.accepted&&(candidate.score>best.score||std::fabs(candidate.score-best.score)<=.10000000149011612F)) {
            best=candidate;result.found=true;
        }
        usable=valid_score(candidate.score);
        if(usable&&candidate.descriptor.kind>=2&&candidate.descriptor.kind<=3&&!candidate.descriptor.flag&&
           candidate.descriptor.target==target&&candidate.side_admitted)backup=candidate;
        return true;
    };
    if(in.try_current) {
        for(unsigned i=0;i<in.count;++i)if(in.neighbors[i].id==retained.descriptor.target)
            retained.side_admitted=side(in.neighbors[i].sides,retained.descriptor.kind);
        bool usable=false;if(!evaluate(retained,0,usable))return false;
        retained_obstacle=retained.obstacle;
        if(retained.side_admitted&&retained.descriptor.target==retained_obstacle)backup=retained;
    }
    if(!result.found) {
        if(!(in.try_current&&retained.descriptor.kind==1&&
             std::fabs(retained.descriptor.parameter)<=.0010000000474974513F)) {
            AiPathCandidate forward;bool usable=false;
            if(!evaluate(forward,retained_obstacle,usable))return false;
        }
        if(!result.found)for(unsigned i=0;i<in.count;++i) {
            const auto& neighbor=in.neighbors[i];if(!neighbor.eligible)continue;
            if(!neighbor.id)return false;
            bool left_valid=false,right_valid=false;
            AiPathCandidate candidate;candidate.descriptor={2,neighbor.id,0,false};
            candidate.side_admitted=side(neighbor.sides,2);
            if(!(in.try_current&&same(candidate.descriptor,retained.descriptor)))
                if(!evaluate(candidate,retained_obstacle,left_valid))return false;
            candidate={};candidate.descriptor={3,neighbor.id,0,false};
            candidate.side_admitted=side(neighbor.sides,3);
            if(!(in.try_current&&same(candidate.descriptor,retained.descriptor)))
                if(!evaluate(candidate,retained_obstacle,right_valid))return false;
            if(!left_valid&&!right_valid&&neighbor.velocity_dot<0) {
                candidate={};candidate.descriptor={neighbor.sides==2?2:3,neighbor.id,0,true};candidate.side_admitted=true;
                bool usable=false;if(!evaluate(candidate,retained_obstacle,usable))return false;
            }
        }
    }
    if(result.found)result.selected=best;
    else {
        bool available=false;if(!ops.available(context,backup.descriptor,available))return false;
        if(available&&retained.collision_time-.10000000149011612F<=backup.collision_time) {
            result.selected=backup;result.selected.side_admitted=true;
        }else if(valid_score(retained.score))result.selected=retained;
        else {
            result.selected={};
            if(!ops.fallback(context,result.selected.geometry.path))return false;
        }
    }
    output=result;return true;
}
bool ai_path_choose_target(const AiTargetPathChoiceInput& in,const AiTargetPathChoiceOps& ops,
    void* context,AiPathChoiceResult& output) noexcept {
    if(!in.target||in.sides>3||!ops.evaluate||!ops.available||!ops.room||!ops.fallback)return false;
    AiPathChoiceResult result;AiPathCandidate retained;retained.descriptor=in.current;
    const auto attempt=[&](const AiContactManeuver& descriptor,bool admitted,AiPathCandidate& candidate) {
        candidate={};candidate.descriptor=descriptor;candidate.side_admitted=admitted;
        if(!ops.evaluate(context,candidate)||!std::isfinite(candidate.score))return false;
        ++result.evaluations;
        if(candidate.accepted){result.selected=candidate;result.found=true;}
        return true;
    };
    if(in.try_current&&(in.current.kind!=1||in.fast)&&in.current_available&&
       ((in.current.kind!=2&&in.current.kind!=3)||in.current.target==in.target)) {
        if(!attempt(in.current,(in.current.kind==2||in.current.kind==3)&&side(in.sides,in.current.kind),retained))return false;
    }
    if(!result.found&&in.fast&&(!in.try_current||!same(in.current,{}))) {
        AiPathCandidate candidate;if(!attempt({},false,candidate))return false;
    }
    AiContactManeuver primary{in.sides==1||in.sides==2?2:3,in.target,0,false};
    AiContactManeuver alternate{primary.kind==2?3:2,in.target,0,false};
    bool room=in.fast;
    // Both source room calls use the primary descriptor, including the
    // alternate-side branch. Preserve that asymmetry.
    if(!result.found&&!in.fast&&!ops.room(context,primary,room))return false;
    if(!result.found&&(!in.try_current||!same(primary,in.current))&&room) {
        AiPathCandidate candidate;if(!attempt(primary,true,candidate))return false;
    }
    if(!result.found&&(!in.try_current||!same(alternate,in.current))&&room) {
        AiPathCandidate candidate;if(!attempt(alternate,false,candidate))return false;
    }
    if(!result.found) {
        bool available=false;if(!ops.available(context,retained.descriptor,available))return false;
        const auto descriptor=available&&valid_score(retained.score)?retained.descriptor:AiContactManeuver{};
        result.selected.descriptor=descriptor;
        if(!ops.fallback(context,descriptor,result.selected.geometry.path))return false;
    }
    output=result;return true;
}
namespace {
struct Planner {
    const AiPathPlannerInput& in;
    const AiPathCollisionWorld& world;
    static bool evaluate(void* p,AiPathCandidate& candidate) {
        const auto& s=*static_cast<Planner*>(p);auto path=s.in.path;
        path.target=nullptr;path.resolved_target=0;
        if(candidate.descriptor.kind!=1) {
            bool found=false;
            for(unsigned i=0;i<s.in.target_count;++i)if(s.in.target_ids[i]==candidate.descriptor.target) {
                path.target=&s.in.targets[i];path.resolved_target=s.in.target_ids[i];
                path.category_available=s.in.target_available[i];found=true;break;
            }
            if(!found)return false;
        }
        return ai_path_evaluate(path,s.in.fitness,{&s.world,ai_path_collision_query},
                               s.in.side_score_scale,s.in.choice.horizon,candidate);
    }
    static bool available(void* p,const AiContactManeuver& d,bool& output) {
        const auto& s=*static_cast<Planner*>(p);output=false;
        if(d.kind==1)output=s.in.path.category_available;
        else if(d.kind==2||d.kind==3)for(unsigned i=0;i<s.in.target_count;++i)
            if(s.in.target_ids[i]==d.target){output=s.in.target_available[i];break;}
        return true;
    }
    static bool fallback(void* p,AiPlannedPath& output) {
        const auto& s=*static_cast<Planner*>(p);auto ex=s.in.path.extension;
        ex.mode=2;ex.target_offset=0;ex.offset_step=99999;ex.margin=s.in.fallback_margin;
        return ai_path_continue(s.in.path.roads,s.in.path.road_count,s.in.path.selected,s.in.path.initial_offset,ex,output);
    }
};
}
bool ai_plan_path(const AiPathPlannerInput& in,const AiPathCollisionWorld& world,AiPathChoiceResult& output) noexcept {
    if(in.target_count>in.targets.size()||in.path.tick!=world.tick||in.fitness.tick!=world.tick)return false;
    Planner planner{in,world};const AiPathChoiceOps ops{Planner::evaluate,Planner::available,Planner::fallback};
    return ai_path_choose(in.choice,ops,&planner,output);
}
bool ai_path_neighbor_policy(const AiRoadChain& chain,const AiNeighborhood& neighbors,unsigned target,
    int category,const AiPathNeighborPolicy& p,bool& eligible,unsigned& sides) noexcept {
    if(neighbors.count>16||neighbors.self_index<0||unsigned(neighbors.self_index)>=neighbors.count||
       target>=neighbors.count||!((category>=0&&category<5)||(category>=11&&category<=16))||chain.count>40)return false;
    for(float f:{p.eligibility_padding,p.width_margin,p.center_limit,p.lateral_limit,p.distant_limit,
                 p.gap_margin,p.curvature_limit})if(!std::isfinite(f))return false;
    const auto& a=neighbors.entries[unsigned(neighbors.self_index)];const auto& b=neighbors.entries[target];
    if(a.cell>=chain.count||b.cell>=chain.count)return false;
    for(float f:{a.distance,b.distance,a.lateral,b.lateral,a.half_extent[0],a.half_extent[1],
                 b.half_extent[0],b.half_extent[1]})if(!std::isfinite(f))return false;
    if(a.id==b.id){eligible=false;sides=0;return true;}
    const float distance=b.distance-a.distance;
    const bool admitted=distance> -((a.half_extent[1]+b.half_extent[1])+p.eligibility_padding);
    float curve=0;if(!ai_road_curvature(chain,a.cell,category,-1,curve))return false;
    const bool ahead=distance-((p.gap_margin+a.half_extent[1])+b.half_extent[1])>0;
    unsigned result=3;
    if(std::fabs(a.lateral)<=p.center_limit&&std::fabs(b.lateral)<=p.center_limit&&
       distance>p.distant_limit&&std::fabs(curve)>=p.curvature_limit)result=curve>0?3U:2U;
    else if(std::fabs(a.lateral-b.lateral)<p.lateral_limit&&ahead)result=1;
    else result=a.lateral>b.lateral?3U:2U;
    const float clearance=(a.half_extent[0]+b.half_extent[0])+p.width_margin;
    const bool left=b.lateral-clearance>=chain.cells[b.cell].lower[category<5?0:unsigned(category-10)];
    const bool right=b.lateral+clearance<=chain.cells[b.cell].upper[category<5?0:unsigned(category-10)];
    if((result==1||result==2)&&!left&&right&&ahead)result=3;
    else if((result==1||result==3)&&!right&&left&&ahead)result=2;
    eligible=admitted;sides=result;return true;
}
bool ai_plan_three_paths(const std::array<AiPathPlannerInput,3>& inputs,
    const AiPathCollisionWorld& world,const std::array<unsigned,3>& self,
    const std::array<std::array<unsigned,16>,3>& eligible,const std::array<unsigned,3>& counts,
    std::array<AiPathChoiceResult,3>& output,unsigned& pair_queries) noexcept {
    if(world.count<4||world.count>world.actors.size())return false;
    for(unsigned i=0;i<3;++i){
        if(self[i]>=world.count||world.actors[self[i]].role!=2)return false;
        for(unsigned j=0;j<i;++j)if(self[i]==self[j])return false;
    }
    std::array<AiPathChoiceResult,3> next;unsigned queries=0;
    for(unsigned i=0;i<3;++i){
        auto view=world;view.self=self[i];view.eligible=eligible[i];view.eligible_count=counts[i];view.pair_queries=0;
        if(!ai_plan_path(inputs[i],view,next[i]))return false;
        queries+=view.pair_queries;
    }
    output=next;pair_queries=queries;return true;
}
} // namespace nfsu
