void run_campaign_sweep(const std::string& root,Screen& screen,PadState& pad,
    bool particles,const std::string& sky,bool glows,bool reflections,TextureFilterMode filter,
    std::uint32_t scene_height,bool gpu_boost,RaceSteering steering,unsigned sweep_event) {
#ifdef NFSU_SWITCH_CAMPAIGN_FINISH_SWEEP
    constexpr bool finish_sweep=true;
#else
    constexpr bool finish_sweep=false;
#endif
    const auto applet=appletGetAppletType();
    require(applet==AppletType_Application||applet==AppletType_SystemApplication,
            "Campaign sweep needs full memory. Open hbmenu with R held on a game, then launch this NRO.");
    struct RestoreAutoSleep {
        bool previous=false,changed=false;
        ~RestoreAutoSleep(){if(changed)appletSetAutoSleepDisabled(previous);}
    } sleep;
    sleep.changed=R_SUCCEEDED(appletIsAutoSleepDisabled(&sleep.previous))&&
                  R_SUCCEEDED(appletSetAutoSleepDisabled(true));

    const auto assets=std::filesystem::path(root).lexically_normal();
    const std::filesystem::path directory="sdmc:/switch/nfsu-" NFSU_RACE_BUILD_TAG;
    const auto retail_path=assets.generic_string();
    const auto log_path=directory.generic_string();
    require(!retail_path.empty() && retail_path!="sdmc:" && retail_path!="sdmc:/" &&
        retail_path!=log_path && log_path.rfind(retail_path+(retail_path.back()=='/'?"":"/"),0)!=0,
        "campaign sweep log directory must be outside retail assets");
    std::filesystem::create_directories(directory);

    screen.show({"NFSU CAMPAIGN SWEEP","Preparing campaign manifest and shared UI..."});
    std::string error;
    CareerCatalog career;
    require(career.load(assets,error),error);
    auto cases=campaign_sweep_cases(career.events(),kCareerLastEvent);
    if(sweep_event!=~0U) {
        cases.erase(std::remove_if(cases.begin(),cases.end(),[&](const auto& test){return test.event!=sweep_event;}),cases.end());
        require(!cases.empty(),"campaign sweep event has no playable races");
    }
    std::vector<CampaignSweepRecord> records;
    records.reserve(cases.size());
    for(const auto& test:cases)records.push_back(read_campaign_sweep_record(directory/test.filename()));

    if(std::any_of(records.begin(),records.end(),[](const auto& row){return row.status==CampaignSweepStatus::pending;})) {
        FrontendArt art;
        require(art.load_startup(root,error)&&art.load_post_race(root,error),error);
        CareerProfile profile;profile.car="CIVIC";profile.name="SWEEP";
        CarAssets car;
        require(load_car_assets(assets/"Global/GLOBALB.BUN",profile.car,car,error),error);
        CareerRideSetup setup;
        {
            std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;
            require(load_race_image(assets/"Global/GLOBALB.BUN",globals,issues),"campaign sweep ride assets unavailable");
            require(resolve_career_ride(globals,career,profile,setup,error),error);
        }
        RaceCarDress dress;dress.request=&setup.request;dress.skin=&setup.skin;
        dress.bind=[](StockFrame& frame,const StockFrameInputs& inputs,const StockSceneRequest& request,std::string& why,void*) {
            return bind_frontend_runtime_skin(frame,request,why)&&bind_frontend_sponsors(inputs,request.installed_parts,frame,why);
        };

        for(std::size_t i=0;i<cases.size()&&appletMainLoop();++i) {
            if(records[i].status!=CampaignSweepStatus::pending)continue;
            padUpdate(&pad);
            if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;
            const auto& test=cases[i];
            const auto path=(directory/test.filename()).string();
            StartupJournal log;
            if(!log.open(path.c_str()))throw std::runtime_error(log.error());
            const auto mark=[&](const char* stage,const std::string& detail) {
                if(!log.mark(stage,detail.c_str(),false))throw std::runtime_error(log.error());
            };
            const auto identity=format("build=%s index=%zu total=%zu event=%u round=%u track=%d direction=%d",
                NFSU_RACE_BUILD_TAG,i+1,cases.size(),test.event,test.round+1,test.track,test.direction);
            mark("case.begin",identity);
            const auto start=armGetSystemTick();
            std::string failure;
            bool cancelled=false;
            try {
                screen.show({"NFSU CAMPAIGN SWEEP",identity,"+ stops the sweep. Progress is saved to SD."});
                SupportedRaceSelection selection;
                require(supported_race_selection(test.event,test.round,selection)&&
                    selection.slot.track==test.track&&selection.slot.direction==test.direction,
                    "runtime event selection differs from the campaign manifest");
                const auto save_base=directory/("career-event-"+std::to_string(test.event));
                if(finish_sweep&&test.round)mark("career.load_round","");
                const auto result_profile=finish_sweep?load_campaign_finish_case(save_base,career,test,profile.car):CareerProfile{};
                CareerTournament start_tournament;
                if(finish_sweep)start_tournament=result_profile.tournament;
                else if(selection.count>1){start_tournament.event=test.event;start_tournament.round=test.round;}
                unsigned completed=0;int style=0;std::array<unsigned,4> places{};
                CareerRaceReport race_report;
                const auto* media=finish_sweep?career.media(test.event):nullptr;
                const auto outcome=run(root,false,false,false,particles,sky,false,glows,true,reflections,filter,
                    scene_height,gpu_boost,true,false,false,test.event?&setup.performance:nullptr,false,
                    steering,car,screen,pad,false,test.event?&dress:nullptr,test.event,&art,profile.difficulty,
                    &completed,media?media->reward[profile.difficulty]:0,result_profile.style_points,&style,
                    media,setup.reputation,finish_sweep?&career:nullptr,result_profile.style_high_water,test.round,&places,
                    start_tournament.event?&start_tournament:nullptr,false,profile.name,&race_report,nullptr,&log,finish_sweep);
                if(finish_sweep) {
                    require(completed==1&&race_report.started&&race_report.won&&
                        outcome==(test.event?RaceReturn::menu:RaceReturn::intro_won),
                        "finish sweep did not return a player win");
                    const bool next_round=test.round+1<selection.count;
                    mark("career.saving","");
                    const auto restored=save_campaign_finish_case(save_base,career,test,next_round,result_profile,places,style,race_report);
                    mark("race.target",format("saved_win=%u next_round=%u bank=%u",unsigned(!next_round),
                        next_round?restored.tournament.round+1:0,restored.bank));
                }else require(outcome==RaceReturn::sweep_complete,"campaign sweep did not reach its runtime target");
            } catch(const Cancel&) {
                cancelled=true;
            } catch(const std::exception& e) {
                failure=e.what();
                if(failure.empty())failure="unspecified race error";
                failure.resize(std::min<std::size_t>(failure.size(),512));
                for(char& c:failure)if(c<32||static_cast<unsigned char>(c)>126)c='?';
                std::printf("CAMPAIGN_SWEEP_ERROR %s reason=%s\n",identity.c_str(),failure.c_str());std::fflush(stdout);
            }
            const auto elapsed=format("elapsed_ms=%.0f",ms(armGetSystemTick()-start));
            if(!cancelled)mark(failure.empty()?"case.pass":"case.fail",
                failure.empty()?elapsed:elapsed+" reason="+failure);
            if(!log.close())throw std::runtime_error(log.error());
            records[i]=read_campaign_sweep_record(path);
            std::printf("CAMPAIGN_SWEEP %s status=%s %s\n",identity.c_str(),
                cancelled?"interrupted":failure.empty()?"passed":"failed",elapsed.c_str());
            std::fflush(stdout);
            if(cancelled)break;
        }
    }

    const auto report_path=directory/"summary.tsv";
    std::ofstream report(report_path,std::ios::binary|std::ios::trunc);
    require(bool(report),"cannot open campaign sweep summary");
    report<<"event\tround\ttrack\tdirection\tstatus\tlast_stage\tlog\n";
    constexpr const char* statuses[]={"pending","interrupted","failed","passed"};
    std::array<unsigned,4> totals{};
    for(std::size_t i=0;i<cases.size();++i) {
        const auto& test=cases[i];const auto& record=records[i];const auto status=unsigned(record.status);
        ++totals[status];
        report<<test.event<<'\t'<<test.round+1<<'\t'<<test.track<<'\t'<<test.direction<<'\t'
              <<statuses[status]<<'\t'<<record.stage<<'\t'<<test.filename()<<'\n';
    }
    report.close();require(bool(report),"cannot write campaign sweep summary");
    const auto result=format("PASS %u / %zu   FAILED %u   INTERRUPTED %u   PENDING %u",totals[3],cases.size(),totals[2],totals[1],totals[0]);
    std::printf("CAMPAIGN_SWEEP %s report=%s\n",result.c_str(),report_path.string().c_str());std::fflush(stdout);
    while(appletMainLoop()) {
        padUpdate(&pad);
        if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;
        screen.show({"NFSU CAMPAIGN SWEEP",result,report_path.string(),"+ exits."});
    }
}
