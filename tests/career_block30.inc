// Run inside the real retail career progression after the event15 award.
{
    const auto retire_presentation=[&](CareerProfile& state){
        if(!catalog.pending_movie(state).empty())check(catalog.movie_finished(state,error),error.c_str());
        if(state.page==CareerPage::unique_reward){
            CareerGarage award;award.reset_unique(catalog,state);
            check(!award.entries().empty(),"new challenge has a compatible unique choice");
            const auto field=award.entries().front().value;bool applied=false;
            check(award.accept(catalog,state,applied,error)&&!applied,"unique explanation precedes choice");
            check(award.accept(catalog,state,applied,error)&&!applied,"unique needs confirmation");
            check(award.accept(catalog,state,applied,error)&&applied,"unique award transaction");
            check(!catalog.award_unique(state,field,0,error),"unique reward cannot be claimed twice");
        }
        if(!catalog.pending_movie(state).empty())check(catalog.movie_finished(state,error),error.c_str());
        for(auto& cover:state.magazine_covers)cover.seen=true;
        state.page=CareerPage::map;
        check(catalog.valid(state,error),error.c_str());
    };
    FrontendArt block_art;
    if(!block_art.load(argv[1],error,false)||!block_art.load_career_pages(argv[1],catalog,error))throw std::runtime_error("full campaign pages: "+error);
    for(unsigned difficulty=0;difficulty<3;++difficulty){
        auto block=reward;block.difficulty=difficulty;
        for(unsigned event=16;event<=30;++event){
            retire_presentation(block);block.selected_event=event;
            const auto open=catalog.available(block);
            check(std::find(open.begin(),open.end(),event)!=open.end(),"authored next-block prerequisites");
            const auto bank=block.bank;const auto installed=block.ride.installed;
            const auto performance=block.ride.performance;const auto unique=block.ride.unique;
            check(catalog.begin_event(block,error),error.c_str());
            const auto* media=catalog.media(event);
            check(media&&!media->before.size(),"no invented before movie in16..30");
            check(block_art.load_career_event(argv[1],event,error),error.c_str());
            FrontendUiFrame ui;block_art.record_career(1280,720,catalog,block,0,"",ui);
            check(!ui.vertices.empty()&&ui.additive.size()<=kFrontendUiMaxQuads,"every new briefing submits original route and text");
            for(unsigned round=0;round<find_race_event(catalog.events(),event)->races.size();++round){
                SupportedRace selected;
                check(load_supported_race(std::filesystem::path(argv[1])/"FrontEnd/FrontB.lzc",
                    std::filesystem::path(argv[1])/"Global/GLOBALB.BUN",event,selected,error,round),error.c_str());
            }
            auto defeated=block;
            if(const auto* tournament=supported_tournament_row(event)){
                for(unsigned r=0;r<tournament->count;++r)
                    check(catalog.tournament_round_finished(defeated,{{4,1,2,3}},error),error.c_str());
            }else check(catalog.event_finished(defeated,event,4,error),error.c_str());
            check(defeated.bank==bank&&defeated.status[event]==(event==26?5:0)&&!defeated.pending_unique&&
                catalog.pending_movie(defeated).empty(),"loss has no money, movie or unique award");
            if(event==26){
                check(catalog.media(event)->reward_speech==26&&catalog.media(event)->speech==25,"TJ briefing and reward use distinct original voices");
                auto timeout=defeated;retire_presentation(timeout);timeout.selected_event=27;
                check(catalog.begin_event(timeout,error),"optional challenge timeout allows original successor");
            }
            const auto save=std::filesystem::path(base.string()+"-block30-"+std::to_string(difficulty));
            if(const auto* tournament=supported_tournament_row(event)){
                for(unsigned r=0;r<tournament->count;++r){
                    check(catalog.tournament_round_finished(block,{{1,2,3,4}},error),error.c_str());
                    if(r+1<tournament->count)check(block.status[event]==0&&block.bank==bank&&block.tournament.round==r+1,
                        "intermediate round cannot finish or pay early");
                    check(save_career(save,catalog,block,error),error.c_str());CareerProfile loaded;
                    check(load_career(save,catalog,loaded,error)&&loaded.tournament.places==block.tournament.places&&
                        loaded.tournament.round==block.tournament.round,"all tournament rounds survive resume");block=std::move(loaded);
                }
            }else check(catalog.event_finished(block,event,1,error),error.c_str());
            check(block.bank==bank+media->reward[difficulty]&&block.status[event]==kEventStatusWon,
                "new event pays authored difficulty prize once");
            check(block.ride.installed==installed&&block.ride.performance==performance&&block.ride.unique==unique,
                "winning unlocks upgrades but cannot silently install them");
            auto duplicate=block;duplicate.selected_event=event;duplicate.page=CareerPage::briefing;
            check(!catalog.event_finished(duplicate,event,1,error)&&duplicate.bank==block.bank,"new result cannot award twice");
            if(event==16)check(catalog.pending_movie(block)=="07_GOD.mad","event16 extro occurs after victory");
            if(event==19||event==25||event==27){
                const auto key=media->magazine_rewards.at(0);
                check(std::any_of(block.magazine_covers.begin(),block.magazine_covers.end(),[&](const auto& cover){
                    return CareerMagazineCatalog::key(cover.index)==key&&cover.car==block.car&&cover.installed==installed;
                }),"event magazine uses actual awarded car snapshot");
            }
            if(event==26){
                check(block.page==CareerPage::unique_reward&&block.pending_unique==26,"second TJ selector precedes return");
                CareerGarage choices;choices.reset_unique(catalog,block);
                check(choices.page()==GaragePage::unique_performance&&choices.entries().size()==3,
                    "TJ26 offers ECU turbo and brakes rather than a hood");
                check(choices.entries()[0].value==23&&choices.entries()[1].value==22&&choices.entries()[2].value==17,
                    "unique category order is authored");
            }
            check(save_career(save,catalog,block,error),error.c_str());CareerProfile loaded;
            check(load_career(save,catalog,loaded,error)&&loaded.status==block.status&&loaded.bank==block.bank&&
                loaded.movie_phase==block.movie_phase&&loaded.pending_unique==block.pending_unique&&
                loaded.magazine_covers.size()==block.magazine_covers.size(),"event continuation is durable");
            block=std::move(loaded);
            for(unsigned slot=0;slot<2;++slot){auto file=save;file+="."+std::to_string(slot);std::filesystem::remove(file);}
        }
        retire_presentation(block);
        check(block.status[30]==kEventStatusWon&&catalog.tuning().category_available(18,2,block.status)&&
            catalog.tuning().category_available(19,2,block.status)&&catalog.tuning().category_available(20,2,block.status),
            "event30 opens three original level2 performance packages");
        auto before=block;before.status[30]=0;
        for(unsigned category:{18U,19U,20U})check(!catalog.tuning().category_available(category,2,before.status),"level2 stays locked before tournament30");
        block.page=CareerPage::garage;block.customization_seen=true;block.bank+=100000;
        check(catalog.buy_performance(block,2,2,error)&&catalog.buy_performance(block,6,2,error)&&
            catalog.buy_performance(block,7,2,error),"earned level2 packages can actually be purchased and installed");
        const auto high=block.style_high_water;block.style_points=0;
        check(block.style_high_water==high&&catalog.valid(block,error),"earned style unlock state persists across point loss");
        block.selected_event=31;block.page=CareerPage::map;
        check(catalog.begin_event(block,error),"campaign continues into event31");
        #include "career_full.inc"
    }
    // A real pre-v8 tournament save has exactly three standings rows.
    {
        auto old=reward;old.page=CareerPage::map;old.selected_event=16;
        auto legacy=std::filesystem::path(base.string()+"-v7-tournament");
        check(save_career(legacy,catalog,old,error),error.c_str());auto slot=legacy;slot+=".0";
        std::ifstream input(slot,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});input.close();
        const auto end=bytes.rfind("SHA256 ");check(end!=std::string::npos,"v8 checksum boundary");
        std::istringstream lines(bytes.substr(0,end));std::vector<std::string> fields;std::string line;
        while(std::getline(lines,line))fields.push_back(line);
        check(fields.size()>48&&fields[0]=="NFSU_CAREER 11","new save schema11");
        // Header0, state1, statuses2, style3, performance4, parts5,
        // ownership6..30, tournament31, standings32..39.
        const auto detailed=std::find_if(fields.begin(),fields.end(),[](const auto& value){return value.rfind("STATS ",0)==0;});
        check(detailed!=fields.end(),"schema10 detailed statistics boundary");fields.erase(detailed,fields.end());
        fields.pop_back(); // Schema9 appends the UI outcome counters.
        fields[0]="NFSU_CAREER 7";fields.erase(fields.begin()+35,fields.begin()+40);
        std::string body;for(const auto& field:fields)body+=field+'\n';
        {std::ofstream output(slot,std::ios::binary|std::ios::trunc);output<<body<<"SHA256 "<<sha256_hex(sha256(body.data(),body.size()))<<'\n';}
        CareerProfile loaded;check(load_career(legacy,catalog,loaded,error)&&loaded.status==old.status&&loaded.ride.installed==old.ride.installed,
            "schema7 car, awards and statuses migrate without consuming later fields");
        for(unsigned i=3;i<loaded.tournament.places.size();++i)check(loaded.tournament.places[i]==std::array<unsigned,4>{},"migrated extra standings are empty");
        for(unsigned i=0;i<2;++i){auto file=legacy;file+="."+std::to_string(i);std::filesystem::remove(file);}
    }
    std::cout<<"PASS campaign16..30 all difficulties, prizes, optional loss, four-round resume, original maps/media, unique choices and level2 purchases\n";
}
