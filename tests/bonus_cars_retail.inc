#include "bonus_cars_native.inc"
void verify_bonus_cars(const std::filesystem::path& root,const std::filesystem::path& capture) {
    std::string error;
    const auto check=[&](bool ok,const std::string& detail){if(!ok)throw std::runtime_error(detail+" / "+error);};
    CareerCatalog catalog,campaign;
    check(catalog.load_free_drive(root,error)&&campaign.load(root,error),"bonus catalogs");
    check(catalog.bonus_cars().size()==std::size(bonus_car_witnesses),"all earned original descriptors");
    check(catalog.quick_cars(0).size()==catalog.starters().size(),"new profile has no earned bonus cars");
    check(campaign.quick_cars(UINT32_MAX).empty(),"Underground trade-in list excludes presets");
    std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;
    check(load_race_image(root/"Global/GLOBALB.BUN",globals,issues),"bonus globals");
    if(!capture.empty())check(std::filesystem::create_directory(capture),"fresh bonus capture directory");
    FrontendArt art;check(art.load(root,error,false)&&art.load_career_pages(root,catalog,error),"bonus UI atlas");
    unsigned rendered=0;
    for(const auto& expected:bonus_car_witnesses) {
        const auto* bonus=catalog.bonus_car(expected.preset);check(bonus!=nullptr,"original preset identity");
        check(bonus->points==expected.points&&bonus->performance==expected.performance,"original threshold and performance stores");
        for(auto points:{expected.points-1,expected.points,expected.points+1}) {
            const auto choices=catalog.quick_cars(points);
            check((std::find(choices.begin(),choices.end(),expected.preset)!=choices.end())==(points>=expected.points),"earned threshold boundary");
            CareerProfile p;p.car="unchanged";
            check(catalog.quick_profile(expected.preset,points,p,error)==(points>=expected.points),"selected preset access boundary");
            if(points<expected.points)check(p.car=="unchanged","locked selection leaves current ride intact");
        }
        CareerProfile p;check(catalog.quick_profile(expected.preset,expected.points,p,error),"earned preset selection");
        check(catalog.valid(p,error),"valid Quick Race preset");
        check(!campaign.valid(p,error),"bonus identity cannot enter campaign save");
        check(!catalog.quick_car_label(p).empty()&&catalog.quick_car_label(p)!=p.car,"bonus display name differs from ordinary model");
        const auto rows=quick_race_rows(QuickRaceMode::circuit,RaceType::circuit,true);
        for(auto row:{QuickRaceRow::customize,QuickRaceRow::performance,QuickRaceRow::unique})
            check(std::find(rows.begin(),rows.end(),row)==rows.end(),"authored bonus ride is fixed");
        CareerRideSetup setup;check(resolve_career_ride(globals,catalog,p,setup,error),"bonus ride assembly");
        for(unsigned slot=0;slot<120;++slot) {
            const auto index=setup.request.installed_parts.records[slot];
            check(index.has_value(),"explicit bonus slot "+std::to_string(slot));
            const int actual=*index==kNoCarPart?-1:int(*index);
            check(actual==expected.parts[slot],std::string(expected.preset)+" native slot "+std::to_string(slot)+" actual="+std::to_string(actual)+" expected="+std::to_string(expected.parts[slot]));
        }
        auto changed=p;changed.ride.performance.fill(3);changed.ride.unique.fill(true);
        check(career_performance_levels(catalog,changed).engine_a==int(expected.performance[2]),"bonus does not inherit ordinary quick tuning");
        check(!career_performance_levels(catalog,changed).nitrous_item,"bonus has no sandbox unique override");
        CareerProfile ordinary;check(catalog.free_profile(p.car,ordinary,error),"return to ordinary model");
        check(ordinary.bonus_preset.empty()&&ordinary.ride.performance==CareerRide{}.performance,"ordinary choice clears preset");
        check(CareerGarage::installed_signature(ordinary)!=CareerGarage::installed_signature(p),"same-model preset refreshes resident preview");
        FrontendGpuScene preview;check(preview.load_garage(root,1280,720,{setup},error),"bonus live preview");
        check(preview.frame(.016F,64,error)&&validate_recorded_commands(preview.commands(),error),"bonus preview commands");
        FrontendUiFrame ui;art.record_free_drive(1280,720,catalog,p,1,0,"",ui);
        check(!ui.vertices.empty(),"bonus car chooser UI");
        if(!capture.empty()) {
            std::vector<unsigned char> rgba;check(preview.render_reference(rgba,error),"bonus preview pixels");
            art.draw_free_drive({rgba.data(),1280,720,1280*4},catalog,p,1,0,"");
            std::ofstream file(capture/(bonus->preset+".tga"),std::ios::binary);
            check(write_tga(file,1280,720,rgba,error),"bonus capture");
        }
        // Use the production Quick Race path, including opponent adaptation,
        // the selected human dress, GO and a frame from the running race.
        const auto courses=catalog.quick_courses(QuickRaceMode::circuit);
        check(!courses.empty(),"bonus circuit course");
        const auto& course=catalog.free_courses().at(courses.front());
        RaceCarDress dress;dress.request=&setup.request;dress.skin=&setup.skin;
        dress.bind=[](StockFrame& frame,const StockFrameInputs& inputs,const StockSceneRequest& request,std::string& err,void*) {
            return bind_frontend_runtime_skin(frame,request,err)&&bind_frontend_sponsors(inputs,request.installed_parts,frame,err);
        };
        QuickRaceOptions options;options.mode=QuickRaceMode::circuit;options.opponents=3;options.traffic=0;
        IntroGrid grid;check(load_race_grid(root,course.event,p.car,options.difficulty,grid,error,&setup.performance,&dress,
            course.round,GridPlayerVisuals::load_with_scene,&options),"bonus starting grid");
        RaceRun run;check(run.set_event(course.event,error,course.round)&&run.set_quick_race(options,error)&&
            run.set_car(p.car,error)&&run.set_performance_levels(setup.performance,error),"bonus race configuration");
        const IntroGridActor* player=nullptr;std::vector<RaceRivalSetup> rivals;std::array<int,3> contexts{};
        for(const auto& actor:grid.actors) {
            if(actor.config.role==Event0Role::human){player=&actor;continue;}
            contexts[rivals.size()]=actor.config.selected_context;
            rivals.push_back({actor.placement_spec,actor.pose,actor.hull,actor.config.event_label,actor.config.visual.car,actor.config.ai_mode});
        }
        check(player&&run.set_intro_grid(player->pose,player->hull,rivals,contexts,error,player->config.final_grid),"bonus runtime grid");
        grid.course={};
        CarAssets assets;check(load_car_assets(root/"Global/GLOBALB.BUN",p.car,assets,error),"bonus race assets");
        RaceGpuScene scene;check(scene.load(posix_file_system(),root,error,nullptr,nullptr,false,true,true,&assets,&dress,
            course.event,false,course.round),"bonus race scene");
        for(auto& actor:grid.actors)if(actor.config.role==Event0Role::opponent)
            check(scene.add_rival(std::move(actor.frame->scene),std::move(actor.base_instances),actor.frame->skin_binding.source,
                std::move(actor.shadow),error,false,actor.glow_anchors,&actor.hull,std::move(actor.neon)),"bonus opponent upload");
        check(run.start(root,scene.track(),error),"bonus race start");
        unsigned driving=0;
        for(unsigned step=0;step<1800&&driving<120;++step) {
            const auto buttons=run.session().timing().go_valid?gamepad_button_bit(GamepadButton::trigger_right):0;
            driving+=buttons!=0;
            check(run.simulate(race_input_frame({1,buttons,0},step),error)&&run.commit(error),"bonus live simulation");
        }
        check(driving==120&&run.session().timing().outcome==RaceOutcome::running,"bonus drives after GO");
        RaceGpuFrame frame;check(scene.frame(run.session(),frame,error)&&validate_recorded_commands(frame.commands,error),"bonus running race frame");
        ++rendered;
        std::cout<<"PASS bonus "<<bonus->preset<<" car="<<p.car<<" points="<<bonus->points<<" parts and preview\n"<<std::flush;
    }
    check(rendered==std::size(bonus_car_witnesses),"every bonus preview loaded");
    CareerProfile earned;
    check(campaign.intro_won(earned,error)&&campaign.movie_finished(earned,error),"earned save setup");
    earned.style_points=0;earned.style_high_water=3000000;
    const auto base=std::filesystem::temp_directory_path()/("nfsu-bonus-save-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    check(save_career(base,campaign,earned,error),"earned high-water save");CareerProfile restored;
    check(load_career(base,campaign,restored,error),"earned high-water reload");
    check(catalog.quick_cars(restored.style_high_water).size()==catalog.starters().size()+rendered,"earned access survives penalty and restart");
    check(catalog.quick_cars(CareerProfile{}.style_high_water).size()==catalog.starters().size(),"fresh profile cannot inherit earned cars");
    for(unsigned slot=0;slot<2;++slot){auto file=base;file+="."+std::to_string(slot);std::filesystem::remove(file);}
    std::cout<<"PASS bonus cars, original parts, earned thresholds, profile isolation and saved access\n";
}
