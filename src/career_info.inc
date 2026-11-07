// Included by career.cpp inside namespace nfsu, outside its anonymous namespace.
void record_career_statistics(CareerStatistics& s,std::uint32_t mask,const CareerRaceReport& r)noexcept {
    if(!r.started)return;
    const auto increment=[](std::uint32_t& value){if(value!=UINT32_MAX)++value;};
    increment(s.starts);increment(r.won?s.wins:s.losses);
    if(std::isfinite(r.metres)&&r.metres>=0&&r.metres<=1e9)s.metres=std::min(1e15,s.metres+r.metres);
    const auto mode=career_statistics_mode(mask);
    if(mode>=0) {
        auto& m=s.modes[unsigned(mode)];increment(m.starts);if(r.won)increment(m.wins);
        constexpr std::int64_t bound=INT64_C(1000000000000000);
        m.style=std::clamp(m.style+std::clamp(r.style,-bound,bound),-bound,bound);
        //4A9FD0 records terminal reason7 only in the drag page.
        if(mode==2&&r.totalled)increment(s.totalled);
    }
}
std::uint32_t CareerCatalog::statistics_track_mask(unsigned event,unsigned round)const {
    const auto* entry=find_race_event(events_,event);
    if(!entry||round>=entry->races.size())throw std::out_of_range("career statistics event/round");
    return statistics_track_masks_.at(entry->races[round].track);
}
CareerInfoValues CareerCatalog::info_values(const CareerProfile& p,const CareerInfoMenu& menu,bool metric)const {
    CareerInfoValues out;
    const auto label=[&](unsigned hash)->const std::string&{return info_labels_.at(hash);};
    out["Controls"]="Left / Right: Mode   B: Back";
    out["LeftEnabled"]=menu.category()?"1":"0";
    out["RightEnabled"]=menu.category()<menu.last()?"1":"0";
    constexpr unsigned headings[]={0xd14b7fcdU,0x817e429aU,0x8d9a96d8U,0x40ed9413U,0xdda5e29eU};
    const auto category=std::min(menu.category(),menu.last());
    out["RaceMode_DATA"]=label(headings[category]);
    if(menu.screen()==CareerInfoScreen::rankings) {
        int latest=-1,rank=-1;
        for(const auto& event:events_.records)if(event.number<p.status.size()&&p.status[event.number]==kEventStatusWon) {
            if(event.kind!=8)latest=std::max(latest,int(event.number));
            if(event.kind==6&&ranking_event_[event.number][0]==int(category)) {
                const int value=ranking_event_[event.number][1];
                if(value>=1&&value<=10&&(rank<0||value<rank))rank=value;
            }
        }
        if(latest==113)latest=112; //5A2690 final campaign record alias.
        //No valid UG profile can open this screen before event0 is won. Keep
        //an invalid preview safe rather than dereference the native null row.
        const bool row_valid=latest>=0&&latest<113;
        const unsigned offsets[]={0,30,10,20};
        const auto name=p.name.empty()?std::string("PLAYER"):p.name;
        for(unsigned row=1;row<=10;++row) {
            const auto key=(row<10?"0":"")+std::to_string(row);
            out["RANK_DATA_"+key]=std::to_string(row);
            const unsigned driver=row_valid?ranking_rosters_[unsigned(latest)][offsets[category]+10-row]:255U;
            out["PlayerName_DATA_"+key]=int(row)==rank?name:driver<ranking_names_.size()?ranking_names_[driver]:"";
        }
        out["Unranked"]=rank<0?"1":"0";
        out["PlayerName_DATA_11"]=name;
    }else {
        for(unsigned row=1;row<=10;++row) {
            const auto key=(row<10?"0":"")+std::to_string(row);
            out["Stats_DATA_"+key]=out["StatsType_DATA_"+key]="";
        }
        const auto bind=[&](unsigned row,unsigned hash,const std::string& value) {
            const auto key="0"+std::to_string(row);
            out["StatsType_DATA_"+key]=label(hash);out["Stats_DATA_"+key]=value;
        };
        const auto& stats=p.statistics;
        if(category<4) {
            constexpr unsigned labels[][3]={{0x7fb43183U,0x6480e3c3U,0xeaaffd6fU},
                {0x836a10f0U,0xb86e03f0U,0xe39cfa9cU},{0x74c345eeU,0x3f59e16eU,0xe855761aU},
                {0x2ce41d89U,0x269a1649U,0xfcfe55f5U}};
            const auto& mode=stats.modes[category];
            bind(1,labels[category][0],std::to_string(mode.starts));
            bind(2,labels[category][1],std::to_string(mode.wins));
            bind(3,labels[category][2],std::to_string(mode.style));
            if(category==2)bind(4,0x49250a46U,std::to_string(stats.totalled));
        }else {
            bind(1,0x0e67ada1U,std::to_string(stats.starts));
            bind(2,0x994ac061U,std::to_string(stats.wins));
            bind(3,0xfdc2f7d9U,std::to_string(stats.losses));
            //4EA8E1..4EA923 truncates a floating point percentage to integer.
            bind(4,0x35d89f9cU,std::to_string(stats.starts?unsigned(double(stats.wins)/stats.starts*100):0));
            char text[64];std::snprintf(text,sizeof text,"%.2f",stats.metres*.001*(metric?1:.6213));
            bind(5,0x78eff98fU,text);bind(6,0x32429d3dU,std::to_string(p.style_points));
        }
        if(stats.partial_history)out["Notice"]="Race details recorded since r102; earlier history unavailable.";
    }
    return out;
}
