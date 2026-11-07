// Owner-requested unrestricted practice. Exercises the actual catalog, garage,
// save boundary and every authored course selection through the runtime loader.
void verify_free_drive(const std::filesystem::path& root,const std::string& capture) {
    std::string error;
    const auto check=[&](bool ok,const std::string& detail){if(!ok)throw std::runtime_error(detail+" / "+error);};
    CareerCatalog catalog,ordinary;
    check(catalog.load_free_drive(root,error),error);check(ordinary.load(root,error),error);
    check(catalog.free_drive()&&!ordinary.free_drive(),"free catalog mode is isolated");
    std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;
    check(load_race_image(root/"Global/GLOBALB.BUN",globals,issues),"free globals input");
    const auto types=parse_car_type_table(globals.data(),globals.size());
    check(catalog.starters().size()==playable_car_names(types).size(),"all playable car records are selectable");
    std::vector<unsigned char> track_image;const auto tracks=load_track_info_table(root/"Global/GLOBALB.BUN",track_image);
    check(tracks.ok(),"original track table");
    for(const auto& t:tracks.records)if(t.mode_mask)check(std::any_of(catalog.free_courses().begin(),catalog.free_courses().end(),[&](const auto& c){return c.track==t.number;}),"playable track missing "+std::to_string(t.number));
    std::vector<CareerRideSetup> prepared;std::vector<CareerProfile> profiles;
    for(const auto& car:catalog.starters()) {
        CareerProfile p;check(catalog.free_profile(car.name,p,error),error);p.page=CareerPage::garage;
        check(catalog.valid(p,error),error);const auto before=p.bank;
        for(unsigned field=0;field<9;++field){check(catalog.buy_performance(p,field,3,error),error);check(catalog.buy_performance(p,field,4,error),error);}
        for(unsigned slot:{1U,2U,6U,7U,8U,10U,11U,13U,14U,15U,17U,30U,34U,115U,116U}) {
            const CarPartRecord* best=nullptr;
            for(const auto& part:catalog.tuning().parts().records)if(catalog.tuning().find_part(p.car,slot,part.part_name_hash)==&part&&part.upgrade_level&&(slot!=17||catalog.tuning().wheel_fits(p.car,part))&&(!best||part.upgrade_level>best->upgrade_level))best=&part;
            if(best)check(catalog.buy_part(p,slot,best->part_name_hash,error),car.name+" slot "+std::to_string(slot)+": "+error);
        }
        check(p.bank==before&&p.ride.owned==CareerRide{}.owned,"free garage consumes no currency or bounded purchase slots");
        check(catalog.valid(p,error),error);CareerRideSetup setup;check(resolve_career_ride(globals,catalog,p,setup,error),error);
        // Every car must publish the real dressed geometry, materials and wheels.
        FrontendGpuScene preview;check(preview.load_garage(root,1280,720,{setup},error),car.name+": "+error);
        check(preview.frame(.016F,64,error),error);check(validate_recorded_commands(preview.commands(),error),error);
        check(preview.commands().draw_count()!=0,"free preview is not empty");
        CareerGarage garage;garage.reset(catalog,p);
        bool purchased=false;check(garage.accept(catalog,p,purchased,error),error); // Visual
        check(garage.accept(catalog,p,purchased,error),error); // Body
        check(garage.accept(catalog,p,purchased,error),error); // Front bumper
        check(std::all_of(garage.entries().begin(),garage.entries().end(),[](const auto& e){return e.available&&e.price==0;}),"free visual catalog has no locks or prices");
        check(std::any_of(garage.entries().begin(),garage.entries().end(),[](const auto& e){return e.part!=0;}),"actual free visual choices");
        prepared.push_back(std::move(setup));profiles.push_back(std::move(p));
        std::cout<<"FREE_CAR "<<car.name<<" tuned preview PASS\n"<<std::flush;
    }
    const auto base=std::filesystem::temp_directory_path()/("nfsu-free-save-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    CareerProfile save_probe;check(!save_career(base,catalog,profiles.front(),error),"free save must be refused");
    check(!load_career(base,catalog,save_probe,error),"free load must be refused");
    check(!std::filesystem::exists(base.string()+".0")&&!std::filesystem::exists(base.string()+".1"),"free mode creates no career slots");
    FrontendArt art;check(art.load(root,error,false),error);check(art.load_career_pages(root,catalog,error),error);
    for(const auto& profile:profiles) {
        CareerGarage garage;garage.reset(catalog,profile);FrontendUiFrame ui;
        art.record_career(1280,720,catalog,profile,0,"",ui,&garage);
        check(!ui.vertices.empty(),"actual garage page for "+profile.car);
    }
    for(unsigned i=0;i<6;++i){FrontendUiFrame ui;art.record_free_drive(1280,720,catalog,profiles.front(),i,unsigned(catalog.free_courses().size()-1),"",ui);check(!ui.vertices.empty()&&ui.additive.size()<kFrontendUiMaxQuads,"free setup GPU page");}
    if(!capture.empty()) {
        check(!std::filesystem::exists(capture),"fresh free capture required");
        FrontendGpuScene scene;check(scene.load_garage(root,1280,720,{prepared.front()},error),error);check(scene.frame(.016F,64,error),error);
        std::vector<unsigned char> rgba;check(scene.render_reference(rgba,error),error);
        art.draw_free_drive({rgba.data(),1280,720,1280*4},catalog,profiles.front(),0,0,"");
        std::ofstream out(std::filesystem::u8path(capture),std::ios::binary);check(write_tga(out,1280,720,rgba,error),error);
    }
    for(unsigned i=0;i<catalog.free_courses().size();++i) {
        const auto& c=catalog.free_courses()[i];const auto index=i%prepared.size();const auto& p=profiles[index];const auto& dress_setup=prepared[index];
        CarAssets car;check(load_car_assets(root/"Global/GLOBALB.BUN",p.car,car,error),error);
        RaceCarDress dress;dress.request=&dress_setup.request;dress.skin=&dress_setup.skin;
        dress.bind=[](StockFrame& frame,const StockFrameInputs& inputs,const StockSceneRequest& request,std::string& err,void*){return bind_frontend_runtime_skin(frame,request,err)&&bind_frontend_sponsors(inputs,request.installed_parts,frame,err);};
        RaceGpuScene scene;scene.set_race_visuals(true);check(scene.set_original_draw_distance(true),"free visibility policy");
        check(scene.load(posix_file_system(),root,error,nullptr,nullptr,false,true,true,&car,&dress,c.event,true,c.round),c.name+": "+error);
        RaceRun run;check(run.set_event(c.event,error,c.round),error);check(run.set_free_drive(true,error),error);
        check(run.set_car(p.car,error),error);check(run.set_performance_levels(dress_setup.performance,error),error);
        check(run.set_crash_camera(kCrashCameraDefaultIntensity,error),"free cinematic configuration");
        check(run.start(root,scene.track(),error),c.name+" start: "+error);
        check(run.session().props()!=nullptr,"free practice owns physical track objects");
        for(unsigned step=0;step<360;++step){check(run.simulate(race_input_frame({0,1,0},run.session().timing().step),error),error);check(run.commit(error),error);}
        RaceGpuFrame frame;check(scene.frame(run.session(),frame,error),error);check(validate_recorded_commands(frame.commands,error),error);
        const auto instances=frame.commands.static_track_instance_count();
        const auto definitions=frame.commands.static_track_definition_count();
        const auto dynamic=frame.commands.draw_count()-frame.commands.static_track_draw_count();
        std::cout<<"FREE_STATIC track="<<c.track<<" instances="<<instances<<" definitions="<<definitions<<" dynamic="<<dynamic<<" capacity="<<scene.draw_capacity()<<"\n"<<std::flush;
        check(deko_static_package_fits(instances,definitions,dynamic,scene.draw_capacity()),
              "free course must pass the shared Switch static-package admission");
        check(frame.car_draws&&frame.track_draws,"free track/car commands are nonempty");
        check(run.session().rival_count()==0,"free mode has no career opponents");
        check(run.session().timing().outcome==RaceOutcome::running,"free practice remains active");
        std::cout<<"FREE_COURSE "<<c.track<<" direction="<<c.direction<<" car="<<p.car<<" PASS\n"<<std::flush;
    }
    std::cout<<"PASS unrestricted free driving catalog, all cars, authored routes, previews and save isolation\n";
}
// Use the standalone roster and traffic through the production scene and session.
void verify_quick_race(const std::filesystem::path& root) {
    std::string error;
    const auto check=[&](bool ok,const std::string& detail){if(!ok)throw std::runtime_error(detail+" / "+error);};
    CareerCatalog catalog;check(catalog.load_free_drive(root,error),"quick race catalog");
    const auto global=root/"Global/GLOBALB.BUN";
    std::vector<unsigned char> image;const auto tracks=load_track_info_table(global,image);
    const auto* olympic=find_track_info(tracks,1003);const auto* highway=find_track_info(tracks,1202);
    check(tracks.ok()&&olympic&&highway,"authored traffic policies");
    for(unsigned density=0;density<4;++density)for(unsigned reverse=0;reverse<2;++reverse) {
        constexpr int expected[]={0,6,10,12};
        check(race_traffic_pool_size(*olympic,density,reverse)==expected[density],"Olympic Square density");
    }
    check(race_traffic_pool_size(*highway,3,0)==22&&race_traffic_pool_size(*highway,3,1)==12,"authored reverse traffic difference");
    constexpr unsigned course_counts[]={16,16,12,8,16,52};
    for(unsigned i=0;i<kQuickRaceModeCount;++i)
        check(catalog.quick_courses(static_cast<QuickRaceMode>(i)).size()==course_counts[i],"quick race authored mode coverage");
    CarAssets car;check(load_car_assets(global,"CIVIC",car,error),"quick race Civic");
    const struct Case {QuickRaceMode mode;int track;unsigned density,opponents,laps;} cases[]={
        {QuickRaceMode::free_run,1003,0,0,0},
        {QuickRaceMode::free_run,1003,1,0,0},
        {QuickRaceMode::free_run,1003,2,0,0},
        {QuickRaceMode::free_run,1003,3,0,0},
        {QuickRaceMode::circuit,1003,1,3,10},
        {QuickRaceMode::circuit,1003,0,0,1},
        {QuickRaceMode::sprint,1103,2,2,1},
        {QuickRaceMode::drag,1202,3,3,1},
        {QuickRaceMode::drift,1301,0,0,2},
        {QuickRaceMode::lap_knockout,1003,1,1,1},
    };
    for(const auto& test:cases) {
        const auto choices=catalog.quick_courses(test.mode);
        const auto chosen=std::find_if(choices.begin(),choices.end(),[&](unsigned i) {
            const auto& c=catalog.free_courses()[i];return c.track==test.track&&!c.direction;
        });
        check(chosen!=choices.end(),"quick race course is selectable");
        const auto& course=catalog.free_courses()[*chosen];
        SupportedRaceSelection selection;
        check(supported_race_selection(course.event,course.round,selection),"course backing");
        const auto type=quick_race_type(test.mode,selection.type);
        QuickRaceOptions options;options.mode=test.mode;options.traffic=test.density;
        options.opponents=test.opponents;options.laps=std::max(1U,test.laps);
        options.catch_up=test.opponents!=2;
        const bool practice=test.mode==QuickRaceMode::free_run;
        const auto name=std::string(quick_race_mode_name(test.mode))+" "+std::to_string(test.track);
        std::cout<<name<<" traffic="<<test.density<<" rivals="<<test.opponents<<std::endl;
        RaceRun run;check(run.set_event(course.event,error,course.round)&&run.set_quick_race(options,error)&&
            run.set_car("CIVIC",error),name+" configure");
        IntroGrid grid;
        const bool has_grid=!practice&&type!=RaceType::drift;
        if(has_grid) {
            check(load_race_grid(root,course.event,"CIVIC",options.difficulty,grid,error,nullptr,nullptr,
                course.round,GridPlayerVisuals::load_with_scene,&options),name+" grid");
            std::vector<RaceRivalSetup> rivals;std::array<int,3> contexts{};
            const IntroGridActor* player=nullptr;
            for(const auto& actor:grid.actors) {
                if(actor.config.role==Event0Role::human){player=&actor;continue;}
                contexts[rivals.size()]=actor.config.selected_context;
                check(contexts[rivals.size()]>=3&&contexts[rivals.size()]<=5,"native medium rival contexts");
                rivals.push_back({actor.placement_spec,actor.pose,actor.hull,actor.config.event_label,actor.config.visual.car,actor.config.ai_mode});
            }
            check(player&&run.set_intro_grid(player->pose,player->hull,rivals,contexts,error,player->config.final_grid),name+" runtime grid");
            grid.course={};
        }else check(load_quick_race_traffic(root,selection,options.traffic,grid.traffic,error),name+" traffic");
        const auto* info=find_track_info(tracks,test.track);
        const auto expected=unsigned(race_traffic_pool_size(*info,test.density,0));
        check(grid.traffic.size()==expected,name+" authored pool size");
        if(has_grid||!grid.traffic.empty()) {
            std::vector<RaceTrafficSetup> traffic;
            for(const auto& actor:grid.traffic)
                traffic.push_back({actor.placement_spec,actor.hull,actor.assets.car_type_name,unsigned(actor.assets.car_type)});
            check(run.set_intro_traffic(traffic,error),name+" traffic configuration");
        }
        RaceGpuScene scene;scene.set_race_visuals(true);scene.set_silhouette_shadows(true);
        check(scene.set_original_draw_distance(true)&&scene.set_complete_zone_backing(practice||type==RaceType::sprint||type==RaceType::drag),name+" scene configuration");
        const bool road_start=has_grid&&type!=RaceType::drag;
        unsigned start_scene=0;
        if(road_start) {
            check(select_road_start_scene(posix_file_system(),root,unsigned(grid.actors.size()),
                grid.setup->desired_player_grid,options.seed,start_scene,error),name+" start scene selection");
            check(scene.set_start_scene(start_scene,selection.region),name+" start scene configuration");
        }
        check(scene.load(posix_file_system(),root,error,nullptr,nullptr,false,true,true,&car,nullptr,course.event,true,course.round),name+" assets");
        if(road_start) {
            RaceStartClock clock;
            check(scene.load_start_camera(posix_file_system(),root,start_scene,selection.region,clock,error)&&
                run.set_start_clock(clock.release,clock.go,error),name+" start camera");
        }
        for(auto& actor:grid.actors)if(actor.config.role==Event0Role::opponent)
            check(scene.add_rival(std::move(actor.frame->scene),std::move(actor.base_instances),actor.frame->skin_binding.source,
                std::move(actor.shadow),error,false,actor.glow_anchors,&actor.hull),name+" rival upload");
        for(auto& actor:grid.traffic)
            check(scene.add_rival(std::move(actor.frame->scene),std::move(actor.base_instances),kNoHandle,
                std::move(actor.shadow),error,true,actor.glow_anchors,&actor.hull),name+" traffic upload");
        check(run.start(root,scene.track(),error),name+" start");
        check(run.session().rival_count()==test.opponents&&unsigned(run.session().course().route.laps)==test.laps,name+" world options");
        check(run.session().is_drag()==(type==RaceType::drag)&&run.session().is_drift()==(type==RaceType::drift),name+" race type");
        const auto* traffic=run.session().traffic();
        check((traffic?traffic->actors().size():0)==expected,name+" live traffic pool");
        for(unsigned step=0,driving=0;step<1800&&driving<120;++step) {
            const auto buttons=run.session().timing().go_valid?gamepad_button_bit(GamepadButton::trigger_right):0;
            driving+=buttons!=0;
            const auto pad=race_input_frame({1,buttons,0},step);
            check(run.simulate(pad,error)&&run.commit(error),name+" live simulation");
        }
        check(run.session().timing().go_valid&&run.session().timing().outcome==RaceOutcome::running,name+" release and running");
        RaceHudAtlas hud;
        check(load_race_hud_atlas(root,hud,error,type!=RaceType::drift&&type!=RaceType::drag,
            false,type==RaceType::drift,test.track),name+" HUD");
        if(type==RaceType::drag)check(load_race_drag_hud(root,"CIVIC",run.session().spec().handling,hud,error),name+" drag HUD");
        check(check_race_persistent_payload({scene.car_bytes(),run.persistent_bytes(),
            scene.track().contacts.memory.resident_bytes,scene.cache_bytes(),0,hud.rgba.size()},error),name+" persistent budget before audio");
        RaceGpuFrame frame;check(scene.frame(run.session(),frame,error)&&validate_recorded_commands(frame.commands,error),name+" frame");
        const auto& commands=frame.commands;
        check(deko_static_package_fits(commands.static_track_instance_count(),commands.static_track_definition_count(),
              commands.draw_count()-commands.static_track_draw_count(),scene.draw_capacity()),name+" Switch command budget");
    }
    std::cout<<"PASS quick race modes, rival setup, traffic levels, grid start and scene admission\n";
}
