#include "career_info_native.inc"
void verify_career_info(const std::filesystem::path& root,const CareerCatalog& catalog,const std::filesystem::path& output) {
    const auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    for(const auto& witness:kNativeCareerDepths)
        check(unsigned(frontend_info_depth_visible(witness.depth))==witness.submitted,"native4F0660 transformed depth admission");
    for(const auto& witness:kNativeCareerArrows) {
        const auto r=frontend_info_rotation(witness.q[0],witness.q[1],witness.q[2],witness.q[3]);
        for(unsigned i=0;i<4;++i)check(std::abs(r[i]-witness.xy[i])<1e-5F,"native arrow quaternion projection and correct direction");
    }
    for(const auto& witness:kNativeCareerPages) {
        CareerInfoMenu menu;menu.open(witness.screen?CareerInfoScreen::statistics:CareerInfoScreen::rankings);
        for(unsigned i=0;i<witness.before;++i)menu.move(1);
        const auto revision=menu.revision();const bool changed=menu.move(witness.direction);
        check(menu.category()==witness.after&&unsigned(changed)==witness.cues,"native4EA090/4EA460 page boundary/cue");
        check(!changed||witness.notification==unsigned(witness.direction<0?1:2),"native navigation notification identity");
        check(menu.revision()==revision,"tab change preserves authored screen Init clock");
        menu.close();check(!menu.active()&&!menu.move(1),"closed information menu cannot consume navigation");
    }
    std::vector<unsigned char> front;std::vector<RaceTableIssue> issues;std::string error;
    check(load_race_image(root/"FrontEnd/FrontB.lzc",front,issues),"original ranking input");
    RaceChunkPayload rank_chunk;check(find_race_chunk(front.data(),front.size(),0x34a03,rank_chunk,issues),"native ranking payload");
    for(const auto& witness:kNativeCareerRanks) {
        CareerProfile p;p.name="RACER";p.selected_event=8;
        for(unsigned i=0;i<=witness.won_through;++i)p.status[i]=kEventStatusWon;
        CareerInfoMenu menu;menu.open(CareerInfoScreen::rankings);
        for(unsigned i=0;i<witness.mode;++i)menu.move(1);
        const auto values=catalog.info_values(p,menu);
        check(values.at("Unranked")==std::string(witness.rank<0?"1":"0"),"native5A1340 ranked/unranked state");
        for(unsigned row=1;row<=10;++row) {
            const auto key=(row<10?"0":"")+std::to_string(row);
            const auto* name=front.data()+rank_chunk.begin+12*witness.drivers[row-1]+4;
            const std::string expected=int(row)==witness.rank?p.name:reinterpret_cast<const char*>(name);
            check(values.at("PlayerName_DATA_"+key)==expected,"native5A2690 latest-event roster, direction and player replacement");
        }
    }
    constexpr unsigned masks[]={1,4,2,8};
    for(const auto& witness:kNativeCareerStats) {
        CareerStatistics stats;
        record_career_statistics(stats,masks[witness.mode],{true,witness.place==1,witness.reason==7,witness.metres,witness.style});
        const auto& mode=stats.modes[witness.mode];
        check(stats.starts==witness.starts&&stats.wins==witness.wins&&stats.losses==witness.losses&&stats.totalled==witness.totalled&&
            mode.starts==witness.mode_starts&&mode.wins==witness.mode_wins&&mode.style==witness.mode_style&&stats.metres==witness.metres,
            "native4ABAA0/4A9FD0 start, outcome, signed style, mileage and drag totalled counters");
    }
    CareerProfile p;check(catalog.intro_won(p,error)&&catalog.movie_finished(p,error)&&catalog.buy(p,0,error)&&catalog.movie_finished(p,error),"information test real career");
    p.name="TEST DRIVER";p.style_points=12345;p.style_high_water=12345;
    for(unsigned i=0;i<4;++i){record_career_statistics(p.statistics,masks[i],{true,true,false,1000.25,250});record_career_statistics(p.statistics,masks[i],{true,false,i==2,900.5,-100});}
    const auto before=p.statistics;
    record_career_statistics(p.statistics,1,{});check(p.statistics.starts==before.starts,"failed load cannot count a start");
    CareerInfoMenu stats;stats.open(CareerInfoScreen::statistics);
    for(unsigned i=0;i<4;++i)stats.move(1);
    const auto values=catalog.info_values(p,stats);
    check(values.at("Stats_DATA_01")=="8"&&values.at("Stats_DATA_02")=="4"&&values.at("Stats_DATA_03")=="4"&&
        values.at("Stats_DATA_04")=="50"&&values.at("Stats_DATA_05")=="4.72"&&values.at("Stats_DATA_06")=="12345", "native GLOBAL labels, integer percentage, USA mileage and style bank");
    check(catalog.info_values(p,stats,true).at("Stats_DATA_05")=="7.60","native metric mileage conversion");
    check(career_statistics_mode(catalog.statistics_track_mask(5,0))==0&&career_statistics_mode(catalog.statistics_track_mask(7,0))==1,
        "knockout and timed challenge follow authored track statistics category");
    const auto folder=std::filesystem::temp_directory_path()/("nfsu-career-info-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);const auto base=folder/"career";
    check(save_career(base,catalog,p,error),"detailed statistics saved");CareerProfile loaded;
    check(load_career(base,catalog,loaded,error)&&loaded.statistics.starts==8&&loaded.statistics.metres==p.statistics.metres&&
        loaded.statistics.modes[2].style==150&&loaded.statistics.totalled==1&&!loaded.statistics.partial_history,"statistics survive slot reload exactly");
    auto file=base;file+=".0";std::ifstream in(file,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});in.close();
    auto body=bytes.substr(0,bytes.find("STATS "));body.replace(0,std::string("NFSU_CAREER 11").size(),"NFSU_CAREER 9");
    const auto legacy=body+"SHA256 "+sha256_hex(sha256(body.data(),body.size()))+'\n';
    {std::ofstream out(file,std::ios::binary|std::ios::trunc);out<<legacy;check(bool(out),"schema9 fixture written");}
    check(load_career(base,catalog,loaded,error)&&loaded.statistics.partial_history&&loaded.statistics.starts==0&&loaded.statistics.metres==0&&
        loaded.style_points==p.style_points,"old profile keeps progress without inventing earlier detailed statistics");
    const auto retry_backup=folder/"career.before-v11.0";
    const auto retry_pending=folder/"career.before-v11.0.pending";
    {std::ofstream out(retry_backup,std::ios::binary);out<<"NFSU_CAREER 9\n";check(bool(out),"interrupted backup fixture written");}
    std::filesystem::create_directory(retry_pending);
    check(!save_career(base,catalog,loaded,error)&&!std::filesystem::exists(folder/"career.1"),"backup failure cannot publish a new live slot");
    check(load_career(base,catalog,loaded,error)&&loaded.style_points==p.style_points,"backup failure keeps the old live slot readable");
    std::filesystem::remove(retry_pending);
    {std::ofstream out(retry_pending,std::ios::binary);out<<"partial";check(bool(out),"interrupted staging fixture written");}
    check(save_career(base,catalog,loaded,error),"schema9 migration to statistics");
    auto backup=base;backup+=".before-v10.0";std::ifstream original(backup,std::ios::binary);std::string saved((std::istreambuf_iterator<char>(original)),{});original.close();
    check(saved==legacy,"pre-v10 rollback is byte exact");
    std::ifstream repaired(retry_backup,std::ios::binary);saved.assign(std::istreambuf_iterator<char>(repaired),{});repaired.close();
    check(saved==legacy&&!std::filesystem::exists(retry_pending),"retry publishes the exact pre-v11 slot and clears staging");
    check(load_career(base,catalog,loaded,error)&&loaded.style_points==p.style_points&&loaded.statistics.partial_history,
        "migrated profile reloads with its progress and statistics state");
    check(catalog.info_values(loaded,stats).count("Notice")==1,"partial history is disclosed in the screen");
    auto invalid=p;invalid.statistics.metres=-1;check(!save_career(base,catalog,invalid,error),"invalid mileage cannot corrupt saves");
    invalid=p;invalid.statistics.modes[1].wins=100;check(!save_career(base,catalog,invalid,error),"invalid mode counters cannot corrupt saves");
    std::filesystem::remove_all(folder);
    FrontendArt art;if(!art.load(root,error,false)||!art.load_career_pages(root,catalog,error))throw std::runtime_error("career information art: "+error);
    const auto atlas_size=art.atlas().rgba.size();
    if(!output.empty()){check(!std::filesystem::exists(output),"fresh information capture directory required");std::filesystem::create_directories(output);}
    for(unsigned screen=0;screen<2;++screen) {
        CareerInfoMenu menu;menu.open(screen?CareerInfoScreen::statistics:CareerInfoScreen::rankings);
        for(unsigned category=0;category<=menu.last();++category) {
            std::vector<unsigned char> rgba(1280*720*4,55);
            for(std::size_t i=3;i<rgba.size();i+=4)rgba[i]=255;
            art.draw_career_info({rgba.data(),1280,720,1280*4},catalog,p,menu);
            unsigned text_pixels=0;
            for(unsigned y=0;y<660;++y)for(unsigned x=0;x<1280;++x) {
                const auto* pixel=rgba.data()+(std::size_t(y)*1280+x)*4;
                if(std::max({pixel[0],pixel[1],pixel[2]})>150)++text_pixels;
            }
            check(text_pixels>1500,"original information labels and values are actually visible in RGB");
            FrontendUiFrame frame;art.record_career_info(1280,720,catalog,p,menu,frame);
            check(!frame.vertices.empty()&&frame.additive.size()<=kFrontendUiMaxQuads&&art.atlas().rgba.size()==atlas_size,"resident original information atlas and bounded GPU commands");
            if(!output.empty()) {
                std::ofstream image(output/((screen?"statistics-":"rankings-")+std::to_string(category)+".ppm"),std::ios::binary);
                image<<"P6\n1280 720\n255\n";for(std::size_t i=0;i<rgba.size();i+=4)image.write(reinterpret_cast<const char*>(rgba.data()+i),3);
                check(bool(image),"information reference image written");
            }
            menu.move(1);
        }
    }
    std::cout<<"PASS native career Rankings/Statistics, durable counters, legacy rollback and nine original FNG pages\n";
}
