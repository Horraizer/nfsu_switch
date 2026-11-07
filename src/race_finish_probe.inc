bool RaceSession::arm_finish_probe(std::string& error) {
    const auto& route=course_.route;
    const auto& track=course_.track;
    if(!started_||!timing_.go_valid||timing_.outcome!=RaceOutcome::running||!route.laps||
       !finish_probe_events_.empty()) {
        error="finish probe requires a live race after GO";return false;
    }
    RaceDrive drive;
    if(!make_race_drive(route,track,false,-1,-1,2.F,drive,error))return false;
    RaceProgress progress;
    start_race_progress(route,track,drive.points.data(),progress);
    if(route.point_to_point)progress.lap=0;
    std::vector<RaceProgressEvent> events,frame;
    std::size_t at=2;
    for(;at+1<drive.points.size();at+=2) {
        frame.clear();
        step_race_progress(route,track,progress,drive.points.data()+at,1.F/60.F,frame);
        for(const auto& e:frame)if(e.kind==RaceEventKind::crossed_forward||e.kind==RaceEventKind::lap_complete)
            events.push_back(e);
        if(progress.finished)break;
    }
    if(!progress.finished||events.empty()||route.finish<0) {
        error="finish probe did not cross the authored finish";return false;
    }
    double x=drive.points[at],y=drive.points[at+1];
    const auto dx=x-drive.points[at-2],dy=y-drive.points[at-1];
    const auto marker=route.boundaries[std::size_t(route.finish)].marker;
    const double z=marker>=0?track.markers[std::size_t(marker)].position[2]:course_.start.ground.z;
    const TrackVehicleGround ground(course_.world);
    auto hit=ground.cast({x,y,z+5},{0,0,-1},15);
    double cast_z=z+5;
    if(!hit.hit&&marker>=0) {
        const auto& finish=track.markers[std::size_t(marker)];
        x=finish.position[0];y=finish.position[1];
        hit=ground.cast({x,y,cast_z},{0,0,-1},15);
    }
    if(!hit.hit) {
        cast_z=z+96;
        hit=ground.cast({x,y,cast_z},{0,0,-1},192);
    }
    if(!hit.hit) {
        // Some drag finish markers stand past the end of the drivable strip.
        // Keep the authored crossing for timing and use nearby approach road
        // for the final scene rather than starting a car over the void.
        const auto first=at>24?at-24:0;
        for(auto i=at;!hit.hit&&i>first;) {
            i-=2;x=drive.points[i];y=drive.points[i+1];
            hit=ground.cast({x,y,cast_z},{0,0,-1},192);
        }
    }
    if(!hit.hit){error="finish probe has no road at the finish";return false;}
    const VehiclePose pose{{x,y,cast_z-hit.distance},std::atan2(dy,dx),10,
        drag_?VehicleGearbox::manual:VehicleGearbox::automatic};
    if(!vehicle_.start(pose)||!chase_.reset(chase_subject(car(),spec_),camera_world_,view_)) {
        error="finish probe car/camera placement refused";return false;
    }
    camera_motion_.reset();
    // Seed a completed drift through the style accumulator. The authored
    // competition still decides the player's place and supplies its UI rows.
    if(spec_.drift_mode) {
        const auto& rule=style_.rule(0);
        if(!rule.enabled||rule.rate<=0||rule.duration<0||rule.duration>5) {
            error="finish probe drift score rule unavailable";return false;
        }
        const auto steps=std::int32_t(std::ceil(rule.duration*60))+3;
        const auto step=race_ticks_per_frame(60);
        for(auto i=steps;i>=0;--i) {
            const auto now=timing_.now_ticks-i*step;
            style_.trigger(0,100000.F/rule.rate,now);
            style_.update(now,1.F/60.F);
        }
        style_.finish(timing_.now_ticks);
        if(style_.item(0).total<=0){error="finish probe drift score was not banked";return false;}
    }
    finish_probe_events_=std::move(events);error.clear();return true;
}
