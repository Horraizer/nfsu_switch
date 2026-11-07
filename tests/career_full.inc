{
unsigned active_event=30;
const auto check=[&](bool ok,const std::string& name){if(!ok)throw std::runtime_error("full event "+std::to_string(active_event)+": "+name+"; "+error);};
std::vector<unsigned char> retail;
check(load_race_event_table(std::filesystem::path(argv[1])/"FrontEnd/FrontB.lzc",retail).ok(),"full campaign original reward bytes");
const auto read32=[](const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);};
// Continue the real saved career after event30; no fabricated prerequisite wins.
for(unsigned event=31;event<=kCareerLastEvent;++event) {
    active_event=event;
    const auto* record=find_race_event(catalog.events(),event);
    if(!record)continue; // Original numbering has no event85.
    retire_presentation(block);block.selected_event=event;
    const auto open=catalog.available(block);
    check(std::find(open.begin(),open.end(),event)!=open.end(),"full campaign prerequisites: "+std::to_string(event));
    check(catalog.begin_event(block,error),error.c_str());
    const auto* media=catalog.media(event);const auto bank=block.bank;
    check(media,"full campaign media");
    const auto check_movie=[&](const std::string& stem) {
        if(!stem.empty())check(std::filesystem::is_regular_file(std::filesystem::path(argv[1])/"Movies"/(stem+".mad")),"authored movie exists: "+stem);
    };
    check_movie(media->before);check_movie(media->after);
    if(!media->before.empty()) {
        check(catalog.pending_movie(block,true)==media->before+".mad","authored late intro request");
        check(catalog.pending_movie(block).empty()==(event!=7&&event!=15&&event!=92),"native movie ordering around difficulty selection");
        check(catalog.movie_finished(block,error),error.c_str());
    }
    check(block_art.load_career_event(argv[1],event,error),error.c_str());
    FrontendUiFrame ui;block_art.record_career(1280,720,catalog,block,0,"",ui);
    check(!ui.vertices.empty()&&ui.additive.size()<=kFrontendUiMaxQuads,"late briefing submits bounded original route");
    const auto complete=[&](CareerProfile& state,unsigned place) {
        if(const auto* tournament=supported_tournament_row(event)) {
            for(unsigned round=0;round<tournament->count;++round) {
                SupportedRace selected;
                check(load_supported_race(std::filesystem::path(argv[1])/"FrontEnd/FrontB.lzc",std::filesystem::path(argv[1])/"Global/GLOBALB.BUN",event,selected,error,round),error.c_str());
                check(catalog.tournament_round_finished(state,place==1?std::array<unsigned,4>{{1,2,3,4}}:std::array<unsigned,4>{{4,1,2,3}},error),error.c_str());
                if(round+1<tournament->count) {
                    check(state.bank==bank&&state.status[event]==0&&state.tournament.round==round+1,"late round cannot pay the tournament prize early");
                    auto mid=std::filesystem::path(base.string()+"-full-round-"+std::to_string(difficulty)+"-"+std::to_string(event));
                    check(save_career(mid,catalog,state,error),error.c_str());CareerProfile resumed_round;
                    check(load_career(mid,catalog,resumed_round,error)&&resumed_round.tournament.places==state.tournament.places,"late tournament round resumes exact standings");
                    state=std::move(resumed_round);
                    for(unsigned slot=0;slot<2;++slot){auto f=mid;f+="."+std::to_string(slot);std::filesystem::remove(f);}
                }
            }
        } else {
            SupportedRace selected;
            check(load_supported_race(std::filesystem::path(argv[1])/"FrontEnd/FrontB.lzc",std::filesystem::path(argv[1])/"Global/GLOBALB.BUN",event,selected,error),error.c_str());
            check(catalog.event_finished(state,event,place,error),error.c_str());
        }
    };
    auto lost=block;complete(lost,4);
    check(lost.bank==bank&&!lost.pending_unique&&!lost.movie_phase,"late loss cannot grant rewards or victory movie");
    check(lost.status[event]==((record->kind==2||record->kind==3)?5:0),"late optional challenge loss follows original availability rule");
    complete(block,1);
    check(block.bank==bank+media->reward[difficulty]&&block.status[event]==kEventStatusWon,"late reward paid once at selected difficulty");
    if(!media->after.empty())check(block.movie_phase==2&&block.movie_event==event,"late victory retains authored extro through reward UI");
    if(!media->unique_categories.empty()) {
        CareerGarage choices;choices.reset_unique(catalog,block);
        check(!choices.entries().empty(),"late unique award has compatible actual parts");
        for(const auto& entry:choices.entries()) {
            auto claim=block;
            check(catalog.award_unique(claim,entry.value,entry.part,error),error.c_str());
            check(!claim.pending_unique&&catalog.valid(claim,error),"each late unique option produces valid ownership");
        }
    }
    auto saved=std::filesystem::path(base.string()+"-full-event-"+std::to_string(difficulty));
    check(save_career(saved,catalog,block,error),error.c_str());CareerProfile loaded;
    check(load_career(saved,catalog,loaded,error)&&loaded.bank==block.bank&&loaded.status==block.status&&loaded.movie_phase==block.movie_phase&&loaded.pending_unique==block.pending_unique,"late result, movie and unique reward survive reload");
    block=std::move(loaded);
    for(unsigned slot=0;slot<2;++slot){auto f=saved;f+="."+std::to_string(slot);std::filesystem::remove(f);}
}
retire_presentation(block);
check(block.status[111]==kEventStatusWon&&block.status[112]==kEventStatusWon&&block.status[113]==kEventStatusWon,"all three final challenges completed");
check(catalog.available(block).empty(),"final win cannot expose debug or completed events");
check(catalog.media(111)->before=="14_MUT"&&catalog.media(111)->after=="16_RES"&&catalog.media(112)->before=="08_EDD"&&catalog.media(112)->after=="17_MET"&&catalog.media(113)->after=="19_FIN","authored finale sequence retained");
for(unsigned category=0;category<25;++category)for(unsigned level=1;level<=4;++level) {
    bool earned=false;
    for(const auto& e:catalog.events().records)if(e.number<=kCareerLastEvent&&block.status[e.number]==kEventStatusWon) {
        const auto* raw=retail.data()+e.offset;
        for(unsigned i=0;i<read32(raw+0x74);++i)
            if(read32(raw+0x38+i*12)==0&&read32(raw+0x3c+i*12)==category&&read32(raw+0x40+i*12)==level)earned=true;
    }
    if(earned)check(catalog.tuning().category_available(category,level,block.status),"every earned final category is unlocked");
}
std::cout<<"PASS full campaign through event113 difficulty="<<difficulty<<" with saved rounds, all unique choices, media and unlocks\n";
}
