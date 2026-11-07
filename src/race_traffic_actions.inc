// Traffic world/action adapter. Pure kernels retain their separate native
// witnesses; this layer supplies actual authored roads, shapes and shared RNG.
struct RaceTraffic::ActionContext {
    RaceTraffic& world;unsigned index;std::uint32_t tick;std::string& error;
    AiTrafficIntersectionRef intersection;
    float intersection_distance=0;
    AiTrafficStopCandidate stop_candidate;
    AiTrafficAvoidanceState avoidance_candidate;
    AiRoadContextResult road_result;
    ActionContext(RaceTraffic& w,unsigned i,std::uint32_t t,std::string& e):world(w),index(i),tick(t),error(e){}
    RaceTrafficActor& actor(){return world.actors_[index];}
    const RacingLineTrack& roads(){return actor().lane>=6?world.backward_lines_:world.lines_;}
    std::uint32_t& random(){return world.world_random_?*world.world_random_:world.rng_;}
    std::uint16_t heading() {
        const auto& a=actor();const auto& f=a.car.rigid_body.axes[0];
        return a.impacted?track_angle(f[0],f[1]):a.published_heading;
    }
    RaceTrafficActionRecord* record(std::uint32_t handle) {
        for(auto& r:actor().action_records)if(r.owned.handle==handle)return &r;return nullptr;
    }
    bool body(std::uint32_t id,const VehicleState*& state,const RaceContactBox*& box) {
        if(id>=1&&id<=4){state=world.racers_[id-1];box=world.racer_shapes_[id-1];return state&&box;}
        if(id>=5&&id-5<world.actors_.size()){const auto& a=world.actors_[id-5];state=&a.car;box=&a.box;return true;}
        return false;
    }
    static bool physical(void* opaque,std::uint32_t id,AiPhysicalObstacleResult& out) noexcept {
        auto& c=*static_cast<ActionContext*>(opaque);const auto& a=c.actor();
        const VehicleState* state=nullptr;const RaceContactBox* box=nullptr;
        if(!c.body(id,state,box)||id==0||id>c.world.world_nodes_.size())return false;
        const auto& target=c.world.world_nodes_[id-1];AiPhysicalObstacleInput in;
        in.self_min={float(a.box.low.x),float(a.box.low.y),float(a.box.low.z)};
        in.self_max={float(a.box.high.x),float(a.box.high.y),float(a.box.high.z)};
        in.position={float(a.car.position.x),float(a.car.position.y),float(a.car.position.z)};
        in.velocity={float(a.car.velocity.x),float(a.car.velocity.y),float(a.car.velocity.z)};
        in.heading=c.heading();in.signed_speed=a.car.owner_speeds.forward;in.absolute_speed=a.car.owner_speeds.magnitude;
        in.other_min={float(box->low.x),float(box->low.y),float(box->low.z)};
        in.other_max={float(box->high.x),float(box->high.y),float(box->high.z)};
        in.other_position=target.position;in.other_forward={target.forward[0],target.forward[1]};
        in.other_velocity={target.velocity[0],target.velocity[1]};
        return ai_physical_obstacle(in,out);
    }
    static bool query(void* opaque,int category,float offset,float clearance,AiObstacleQueryResult& out) {
        auto& c=*static_cast<ActionContext*>(opaque);
        return ai_obstacle_query(c.actor().neighbors,{category,offset,clearance,c.actor().car.owner_speeds.magnitude},physical,&c,out);
    }
    static bool admitted(void* opaque,std::uint32_t id,bool& yes) {
        auto& c=*static_cast<ActionContext*>(opaque);yes=true;
        if(!c.intersection.present||id<5)return true;
        if(id-5>=c.world.actors_.size())return false;
        bool paired=false;
        if(!ai_traffic_intersection_pair(c.world.intersections_[c.intersection.index],50+int(c.index),3,
            50+int(id-5),3,paired))return false;
        yes=!paired;return true;
    }
    static bool transition(void* opaque,int from,int to,bool& yes) {
        auto& c=*static_cast<ActionContext*>(opaque);const auto& a=c.actor();yes=false;
        if(to<11||to>16)return true;
        const auto& self=a.neighbors.entries[unsigned(a.neighbors.self_index)];
        if(self.cell>=a.road.chain.count)return false;
        const auto& p=a.road.chain.cells[self.cell];
        const bool available=a.road.flags.enabled[unsigned(to-10)]&&
            ai_road_category_available(c.roads(),{p.point.route_index,p.point_index},to);
        AiLaneTransitionInput in;in.track_number=int(c.world.track_number_);in.direction=a.lane>=6;
        in.progress=a.progress;in.controller_kind=4;
        return ai_road_lane_transition(in,from,to,available,yes);
    }
    static bool nearest(void* opaque,int& lane) {
        auto& c=*static_cast<ActionContext*>(opaque);const auto& a=c.actor();
        const auto& self=a.neighbors.entries[unsigned(a.neighbors.self_index)];
        if(self.cell>=a.road.chain.count)return false;
        const auto& p=a.road.chain.cells[self.cell];const auto* r=c.roads().find(p.point.route_index);
        if(!r)return false;
        lane=ai_road_nearest_lane(*r,p.point_index,{float(a.car.position.x),float(a.car.position.y),float(a.car.position.z)});
        return true;
    }
    static bool is_traffic(void* opaque,std::uint32_t id,bool& yes) {
        auto& c=*static_cast<ActionContext*>(opaque);yes=id>=5&&id-5<c.world.actors_.size();
        return yes||(id>=1&&id<=4&&c.world.racers_[id-1]);
    }
    AiTrafficAvoidanceInput avoidance_input(){auto& a=actor();return {&a.neighbors,a.road.category,a.car.owner_speeds.magnitude,24.58650016784668F};}
    static AiTrafficAvoidanceOps avoidance_ops(){return {query,admitted,transition,nearest,is_traffic};}
    bool prepare() {
        auto& a=actor();const auto direction=unsigned(a.lane>=6);
        for(auto& table:world.intersections_)if(!ai_traffic_intersection_clear(table,50+int(index)))return false;
        AiRoadContextInput in;in.track_number=int(world.track_number_);
        in.selection.position={float(a.car.position.x),float(a.car.position.y),float(a.car.position.z)};
        in.selection.progress=a.progress;in.selection.maximum_distance=999999.F;
        in.selection.first_route=0;in.selection.last_route=99;in.selection.role=3;
        in.selection.require_enabled_lane=true;in.selection.allow_outside_width=true;
        in.body_width=float(a.box.high.y-a.box.low.y);in.back_margin=24;
        // Actual traffic virtual24=false,50=4s,64=true. Geometry is already
        // oriented to this actor's travel direction; event policy stays raw.
        in.keep_previous=false;in.allow_wide=true;in.controller_kind=4;in.ride_category=int(a.ride_category);
        AiRoadHorizons horizons;if(!ai_road_horizons(a.car.owner_speeds.magnitude,4,4,horizons))return false;
        in.initial_horizon=horizons.initial;in.retained_horizon=horizons.retained;
        AiRoadPolicyInput policy;policy.direction=int(direction);policy.role=3;policy.policy=AiRoadDecisionPolicy::traffic;
        const float length=world.course_->route.spline.path_length;
        if(!ai_road_expire(world.events_,a.progress,length,!world.course_->route.spline.open,!direction,10,a.road_policy)||
           !ai_road_update_context(roads(),world.events_,world.route_reverse_[direction],in,policy,a.road_policy,
               random(),a.road,road_result))return false;
        if(!a.road.selected||a.road.nearest<0||!a.road.chain.count)return world.retire(index,error);
        if(a.road.category<11||a.road.category>16)return false;
        a.lane=direction*6+unsigned(a.road.category-11);a.cursor=a.road.road;
        if(road_result.reset_maneuver)a.maneuver={};
        if(!ai_road_locate_actor(a.road.chain,in.selection.position,a.road.category,a.location))return false;
        auto nodes=world.world_nodes_;
        if(direction)for(auto& node:nodes)node.progress=length-node.progress;
        AiNeighborInput ni;const auto& node=nodes[4+index];ni.self_id=node.id;
        ni.position=node.position;ni.velocity=node.velocity;ni.forward=node.forward;ni.heading=heading();
        ni.progress=node.progress;ni.horizon=horizons.retained;ni.track_length=length;
        ni.category=a.road.category;ni.ride_category=int(a.ride_category);ni.circuit=!world.course_->route.spline.open;
        if(!ai_neighbors(a.road.chain,ni,nodes.data(),nodes.size(),a.neighbors)||a.neighbors.self_index<0)return false;
        const auto& self=a.neighbors.entries[unsigned(a.neighbors.self_index)];
        bool end=false;if(!ai_road_stop_event_reached(a.road.chain,a.road.flags.event_flag,self.distance,end))return false;
        if(end)return world.retire(index,error);
        if(!ai_traffic_intersection_reference(world.lines_,a.road.chain,a.road.flags.first_limited,
            world.intersections_.size(),intersection))return false;
        if(intersection.present) {
            if(!ai_traffic_intersection_distance(world.lines_,world.intersections_,a.road.chain,
                a.road.flags.first_limited,self,direction,intersection_distance))return false;
            float arrival=0;if(!ai_traffic_intersection_arrival(intersection_distance,a.car.owner_speeds.forward,arrival)||
                !ai_traffic_intersection_register(world.intersections_[intersection.index],50+int(index),3,
                    a.road.road.route,intersection_distance,arrival))return false;
        }
        return true;
    }
    static bool evaluate(void* opaque,AiTrafficStrategyQuery which,bool& found,AiActionCandidate&) {
        auto& c=*static_cast<ActionContext*>(opaque);
        if(which==AiTrafficStrategyQuery::obstacle){
            if(!ai_traffic_stop_offer(c.intersection,c.random(),c.stop_candidate))return false;
            found=c.stop_candidate.priority>=0;return true;
        }
        return ai_traffic_avoidance_offer(c.avoidance_input(),avoidance_ops(),&c,c.avoidance_candidate,found);
    }
    bool allocate(int kind,int priority,AiOwnedAction& out) {
        for(auto& r:actor().action_records)if(!r.owned.handle) {
            r={};r.owned={actor().next_action_handle++,kind,priority,false,0};
            if(!r.owned.handle)return false;
            if(kind==3)r.avoidance=avoidance_candidate;
            if(kind==18)r.stop={stop_candidate.intersection,stop_candidate.mode,false};
            out=r.owned;return true;
        }
        return false;
    }
    static bool predicate(void* opaque,const AiOwnedAction& action,AiActionPredicate which,bool& yes) {
        auto& c=*static_cast<ActionContext*>(opaque);auto* r=c.record(action.handle);if(!r)return false;
        yes=false;if(which!=AiActionPredicate::complete)return true;
        if(action.kind==18)return ai_traffic_stop_complete(c.intersection,c.random(),r->stop,yes);
        if(action.kind==3)return ai_traffic_avoidance_complete(c.avoidance_input(),avoidance_ops(),&c,r->avoidance,yes);
        return action.kind==8;
    }
    static bool choose(void* opaque,int kind,int priority,AiOwnedAction& out) {
        auto& c=*static_cast<ActionContext*>(opaque);AiActionCandidate candidate;
        return ai_traffic_strategy_choose(kind,priority,evaluate,&c,candidate)&&c.allocate(candidate.kind,candidate.priority,out);
    }
    static bool initial(void* opaque,AiOwnedAction& out){return static_cast<ActionContext*>(opaque)->allocate(8,0,out);}
    static bool cleanup(void* opaque,const AiOwnedAction& action){auto& c=*static_cast<ActionContext*>(opaque);if(!c.record(action.handle))return false;c.actor().actor_override_enabled=false;return true;}
    static void release(void* opaque,const AiOwnedAction& action){auto& c=*static_cast<ActionContext*>(opaque);if(auto* r=c.record(action.handle))*r={};}
    static bool promote(void* opaque){auto& c=*static_cast<ActionContext*>(opaque);bool activated=false;return c.world.prepare_contact(c.index,c.error,activated);}
    static bool update(void* opaque,const AiOwnedAction& action) {
        auto& c=*static_cast<ActionContext*>(opaque);auto& a=c.actor();auto* r=c.record(action.handle);if(!r)return false;
        AiActionSpeedState speed;
        if(action.kind==8) {
            if(float(std::int32_t(c.tick-r->epoch))*.00025F>=3.F||a.maneuver.kind!=1)r->epoch=c.tick;
            a.maneuver={};speed={24.58650016784668F,0,2,false,true,true,true};
        }else if(action.kind==3) {
            AiTrafficAvoidanceOutput out;
            if(!ai_traffic_avoidance_update(c.avoidance_input(),avoidance_ops(),&c,r->avoidance,out))return false;
            a.road.category=out.category;a.lane=(a.lane>=6?6:0)+unsigned(out.category-11);
            if(out.reset_maneuver)a.maneuver={};
            speed=out.speed;a.actor_override_enabled=out.actor_override_enabled;
            if(out.actor_override_enabled)a.actor_override=out.actor_override;
        }else if(action.kind==18) {
            if(!c.intersection.present||r->stop.intersection!=int(c.intersection.index))return false;
            AiTrafficStopInput in;in.route=a.road.road.route;in.distance=c.intersection_distance;
            in.absolute_speed=a.car.owner_speeds.magnitude;in.controller_cap=24.58650016784668F;
            const auto& self=a.neighbors.entries[unsigned(a.neighbors.self_index)];
            if(!query(&c,a.road.category,self.lane_lateral[unsigned(a.road.category-11)],99999.F,in.obstacle)||
                (in.obstacle.id&&!admitted(&c,in.obstacle.id,in.obstacle_admitted)))return false;
            AiTrafficStopState state{r->stop.stopped};AiTrafficStopOutput out;
            if(!ai_traffic_stop_update(in,c.world.intersections_[c.intersection.index],state,out))return false;
            r->stop.stopped=state.stopped;speed=out.speed;a.actor_override_enabled=out.actor_override_enabled;
            if(out.actor_override_enabled)a.actor_override=out.actor_override;
        }else return false;
        if(!ai_road_locate_actor(a.road.chain,{float(a.car.position.x),float(a.car.position.y),float(a.car.position.z)},a.road.category,a.location))return false;
        AiActionPathInput input;input.roads=a.road.chain.cells.data();input.road_count=a.road.chain.count;
        input.selected=a.location.cell;input.initial_offset=a.location.lateral;input.signed_speed=a.car.owner_speeds.forward;
        input.policy.absolute_speed=a.car.owner_speeds.magnitude;input.policy.physics_selector=4;
        input.extension.category=a.road.category;input.extension.actor_position={float(a.car.position.x),float(a.car.position.y),float(a.car.position.z)};
        input.extension.actor_velocity={float(a.car.velocity.x),float(a.car.velocity.y),float(a.car.velocity.z)};
        input.extension.actor_forward=a.car.rigid_body.axes[0];input.extension.absolute_speed=a.car.owner_speeds.magnitude;
        input.extension.clearance=float(a.box.high.y-a.box.low.y)*.5F;input.extension.margin=1;
        input.tick=c.tick;input.retain_parameters={16,0,0};input.previous.path=a.published_path;
        input.rebuild=c.road_result.reselected||!a.published_path.path.count;
        input.category_available=ai_road_category_available(c.roads(),a.road.road,a.road.category);
        AiManeuverResult path;if(!ai_action_plan_path(a.maneuver,input,path))return false;
        a.published_path=path.path;auto driver=path.path;
        if(!ai_path_publish(driver))return false;a.driver_path=driver;a.path=driver.path;a.action_speed=speed;
        const auto& self=a.neighbors.entries[unsigned(a.neighbors.self_index)];
        AiTrafficSpeedInput limit;limit.first=self.cell;limit.category=a.road.category;
        limit.distance=self.distance;limit.absolute_speed=a.car.owner_speeds.magnitude;limit.cap=24.58650016784668F;
        limit.stopped=a.crash_stop;limit.route_speed=speed.route_speed;limit.action={speed.speed,speed.duration,speed.auxiliary};
        std::array<float,3> result;if(!ai_traffic_speed(a.road.chain,limit,result))return false;
        a.target_speed=result[0];a.ended=false;return true;
    }
};
bool RaceTraffic::retire(unsigned index,std::string& error) {
    if(index>=actors_.size()){error="invalid traffic retirement";return false;}
    for(auto& table:intersections_)if(!ai_traffic_intersection_clear(table,50+int(index))){error="traffic intersection retirement failed";return false;}
    auto& a=actors_[index];if(a.active){a.active=false;a.previous_valid=false;++retirements;}return true;
}
bool RaceTraffic::prepare_world(std::string& error) {
    const float length=course_->route.spline.path_length;
    for(unsigned id=1;id<=world_nodes_.size();++id) {
        auto& node=world_nodes_[id-1];node={};node.id=id;node.enabled=false;
        const VehicleState* state=nullptr;const RaceContactBox* box=nullptr;TrackPathCoordinate* coordinate=nullptr;
        if(id<=4){state=racers_[id-1];box=racer_shapes_[id-1];coordinate=&racer_coordinates_[id-1];if(!state||racer_ghosts_[id-1])continue;}
        else {if(id-5>=actors_.size()||!actors_[id-5].active)continue;auto& a=actors_[id-5];state=&a.car;box=&a.box;coordinate=&a.coordinate;}
        if(!box){error="traffic racer shape unavailable";return false;}
        const auto& s=*state;node.enabled=true;node.position={float(s.position.x),float(s.position.y),float(s.position.z)};
        node.velocity={float(s.velocity.x),float(s.velocity.y),float(s.velocity.z)};
        const auto& q=s.orientation;
        node.forward=s.rigid_body_ready?s.rigid_body.axes[0]:AiDriverPoint{{float(1-2*(q.y*q.y+q.z*q.z)),float(2*(q.x*q.y+q.w*q.z)),float(2*(q.x*q.z-q.w*q.y))}};node.bounds_min={float(box->low.x),float(box->low.y)};node.bounds_max={float(box->high.x),float(box->high.y)};
        const float point[]={node.position[0],node.position[1]};
        update_track_path_coordinate(course_->route.spline,course_->track,*coordinate,point);
        node.progress=course_->route.reverse?length-coordinate->distance:coordinate->distance;
        if(id>=5)actors_[id-5].progress=node.progress;
    }
    return true;
}
bool RaceTraffic::plan(unsigned index,std::uint32_t tick,std::string& error) {
    ActionContext context{*this,index,tick,error};
    if(!context.prepare()){if(error.empty())error="traffic road/world preparation refused";return false;}
    auto& a=actors_[index];if(!a.active)return true;
    a.action_owner.physical_selector=a.impacted?3:2;
    const AiTrafficActionOwnerOps ops{ActionContext::predicate,ActionContext::choose,ActionContext::initial,
        ActionContext::cleanup,ActionContext::release,ActionContext::promote,ActionContext::update};
    if(!ai_traffic_action_owner_step(ops,&context,a.action_owner)){if(error.empty())error="traffic action owner refused";return false;}
    ++decisions;return true;
}
