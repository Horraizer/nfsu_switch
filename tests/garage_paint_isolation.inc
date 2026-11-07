// Real menu navigation, resident preview bank and identical-pose pixel checks.
namespace {
void verify_paint_isolation(const std::filesystem::path& root,const CareerCatalog& catalog,
                            const std::filesystem::path& save_base) {
    const auto check=[](bool ok,const std::string& e){if(!ok)throw std::runtime_error(e);};
    CareerProfile p;std::string error;check(catalog.intro_won(p,error)&&catalog.movie_finished(p,error),error);
    const auto car=std::find_if(catalog.starters().begin(),catalog.starters().end(),[](const auto& c){return c.name=="CIVIC";});
    check(car!=catalog.starters().end(),"Civic absent");check(catalog.buy(p,std::size_t(car-catalog.starters().begin()),error)&&catalog.movie_finished(p,error),error);
    p.bank=100000;p.customization_seen=true;
    FrontendArt art;check(art.load(root,error,false)&&art.load_career_pages(root,catalog,error),error);
    const auto& tuning=catalog.tuning();const auto& db=tuning.parts();
    // Event6 unlocks an engine package; enter it and finish via the real
    // result transaction so selection of the next event cannot affect the arrow.
    p.selected_event=6;p.page=CareerPage::map;
    for(unsigned n=1;n<6;++n)p.status[n]=kEventStatusWon;
    check(catalog.media(6)->new_part_levels[18]==1,"event6 engine reward absent");
    check(catalog.begin_event(p,error),error);
    for(unsigned i=0;i<3;++i)check(catalog.tournament_round_finished(p,{{1,2,3,4}},error),error);
    check(save_career(save_base,catalog,p,error),error);
    CareerProfile restored;check(load_career(save_base,catalog,restored,error),error);
    auto slot=save_base;slot+=".0";std::filesystem::remove(slot);
    check(restored.new_parts_event==6&&restored.status[6]==kEventStatusWon,"reward context survives save and load");
    p=std::move(restored);
    p.page=CareerPage::garage;
    {
        CareerGarage menu;menu.reset(catalog,p);bool bought=false;
        menu.move(1);
        check(menu.entries()[menu.selected()].newly_unlocked,"performance arrow after event6 victory");
        check(menu.accept(catalog,p,bought,error),"open performance");
        check(menu.entries()[menu.selected()].value==2&&menu.entries()[menu.selected()].newly_unlocked,"only awarded category gets its arrow");
        check(menu.accept(catalog,p,bought,error),"open engine packages");
        check(!menu.entries()[0].newly_unlocked&&menu.entries()[1].newly_unlocked&&!menu.entries()[2].newly_unlocked,
            "arrow marks the newly unlocked level, not stock or a locked package");
        menu.move(1);
        art.advance(.1F); // Sample the bright phase of the authored arrow.
        FrontendUiFrame marked,plain;
        std::vector<unsigned char> marked_pixels(1280*720*4),plain_pixels(marked_pixels.size());
        art.record_career(1280,720,catalog,p,0,"",marked,&menu);
        art.draw_career({marked_pixels.data(),1280,720,1280*4},catalog,p,0,"",&menu);
        const auto unlock=p.new_parts_event;p.new_parts_event=0;menu.refresh(catalog,p);
        art.record_career(1280,720,catalog,p,0,"",plain,&menu);
        art.draw_career({plain_pixels.data(),1280,720,1280*4},catalog,p,0,"",&menu);
        check(marked.vertices.size()==plain.vertices.size()+4,"one authored unlock arrow reaches the UI draw list");
        check(marked_pixels!=plain_pixels,"authored unlock arrow changes visible pixels");
        p.new_parts_event=unlock;
    }
    for(const auto& e:catalog.events().records)p.status[e.number]=kEventStatusWon;
    p.style_high_water=1000000;
    {
        p.new_parts_event=18; // The authored event18 reward is level2 neon.
        CareerGarage menu;menu.reset(catalog,p);bool bought=false;
        check(menu.entries()[0].newly_unlocked&&!menu.entries()[1].newly_unlocked,"visual unlock only marks the Visual menu");
        check(menu.accept(catalog,p,bought,error),error);menu.move(2);
        check(menu.entries()[menu.selected()].newly_unlocked,"accessories arrow after neon reward");
        check(menu.accept(catalog,p,bought,error)&&menu.entries()[0].newly_unlocked&&!menu.entries()[1].newly_unlocked,error);
        check(menu.accept(catalog,p,bought,error),error);
        unsigned marked=0;
        for(const auto& entry:menu.entries())if(entry.newly_unlocked) {
            const auto* part=tuning.find_part(p.car,116,entry.part);
            check(part&&part->upgrade_level==2,"neon arrow marks only the awarded level");
            ++marked;
        }
        check(marked!=0,"neon reward marks actual parts");
    }
    check(catalog.buy_performance(p,2,3,error),error);
    std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;
    check(load_race_image(root/"Global/GLOBALB.BUN",globals,issues),"global read failed");
    FrontendGpuScene scene;
    CareerCatalog practice;check(practice.load_free_drive(root,error),error);
    struct PaintCase {unsigned slot;unsigned wheel_level;const char* car;unsigned engine_level=3;};
    const PaintCase cases[]={{41,1,"CIVIC"},{41,2,"CIVIC"},{41,3,"SKYLINE"},
        {37,0,"CIVIC"},{37,0,"SKYLINE",2},{37,0,"SKYLINE"},{30,0,"SKYLINE"}};
    for(const auto& test:cases) {
        const auto slot=test.slot;
        const auto& shop=std::string(test.car)=="CIVIC"?catalog:practice;
        auto profile=p;
        if(&shop==&practice) {
            check(shop.free_profile(test.car,profile,error),error);
            profile.page=CareerPage::garage;
            check(shop.buy_performance(profile,2,test.engine_level,error),error);
        }
        const auto& parts=shop.tuning();
        if(test.wheel_level) {
            const auto wheel=std::find_if(db.records.begin(),db.records.end(),[&](const auto& x) {
                return x.slot==17&&x.upgrade_level==test.wheel_level&&parts.wheel_fits(profile.car,x);
            });
            check(wheel!=db.records.end(),"fitted rim absent");
            check(shop.buy_part(profile,17,wheel->part_name_hash,error),error);
            CareerGarage rim;bool bought=false;rim.reset(shop,profile);
            check(rim.accept(shop,profile,bought,error),error);rim.move(1);
            check(rim.accept(shop,profile,bought,error),error);
            const auto brand=std::find_if(rim.entries().begin(),rim.entries().end(),[&](const auto& entry){return entry.value==wheel->hash_0c;});
            check(brand!=rim.entries().end(),"installed rim brand absent");
            rim.move(int(brand-rim.entries().begin())-int(rim.selected()));
            check(rim.accept(shop,profile,bought,error),error);
            check(rim.entries()[rim.selected()].installed&&rim.preview(profile).ride.installed[17]==wheel->part_name_hash,
                "opening the rim family retains the exact installed size");
            check(rim.accept(shop,profile,bought,error),error);
            check(rim.entries()[rim.selected()].installed&&rim.entries()[rim.selected()].upgrade_level==test.wheel_level,
                "size picker selects the installed diameter and carries its original tier");
            check(rim.entries().size()>1,"native size picker offers multiple diameters");
            for(const auto& entry:rim.entries()) {
                const auto* record=parts.find_part(profile.car,17,entry.part);
                check(record&&parts.ownership_key(*record)==parts.ownership_key(*wheel)&&parts.wheel_fits(profile.car,*record,false),
                    "size picker contains only fitted sizes of the selected family");
            }
        }
        CareerGarage menu;menu.reset(shop,profile);bool purchased=false;
        check(menu.accept(shop,profile,purchased,error),error);menu.move(3);check(menu.accept(shop,profile,purchased,error),error);
        const auto at=std::find_if(menu.entries().begin(),menu.entries().end(),[&](const auto& e){return e.value==slot;});
        check(at!=menu.entries().end(),"paint category absent");menu.move(int(at-menu.entries().begin()));check(menu.accept(shop,profile,purchased,error),error);
        for(unsigned i=0;i<3;++i)menu.move_color(1,0);
        const auto first=menu.preview_index();const auto before=profile.ride.installed[slot];
        std::vector<CareerRideSetup> variants;std::vector<unsigned> indices;
        const auto resolving=std::chrono::steady_clock::now();
        check(menu.prepare_previews(globals,shop,profile,variants,indices,error),error);
        const auto resolve_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-resolving).count();
        check(first==3&&indices[first]&&indices[4],"two paint swatches are resident");
        const auto first_material=car_part_material_hash(db,*variants[indices[first]].request.installed_parts.records[slot]);
        check(slot==30||first_material==car_part_material_hash(db,*variants[indices[4]].request.installed_parts.records[slot]),
            "two chosen swatches share the same material");
        const auto signature=menu.preview_signature(profile);
        const auto started=std::chrono::steady_clock::now();
        FrontendGpuScene bank;
        check(bank.load_garage(root,1280,720,variants,error,nullptr,scene.environment(),scene.car_source()),error);
        const auto prepared_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        check(scene.adopt_garage(bank,error,indices[first]),error);
        scene.focus_part(menu.focus_slot());scene.road_target(menu.road_target());
        for(unsigned i=0;i<90;++i)check(scene.frame(1.F/60,1000,error),error);
        std::vector<unsigned char> a,b;
        check(scene.render_reference(a,error),error);
        menu.move_color(1,0);
        const auto second=menu.preview_index();
        check(second==4&&profile.ride.installed[slot]==before,"highlight previews without buying");
        check(scene.select_part(indices[second],error)&&scene.frame(0,1000,error),error);
        check(scene.render_reference(b,error),error);
        check(a!=b,"two paint swatches with the same material must change rendered pixels");
        const auto last=unsigned(menu.entries().size()-1);
        check(last>=30&&indices[last],"tier 3 swatch is resident in the same bank");
        menu.move(int(last)-int(second));
        check(menu.preview_index()==last&&menu.preview_signature(profile)==signature,
            "moving to another paint tier does not request a new bank");
        check(scene.select_part(indices[last],error)&&scene.frame(0,1000,error),error);
        std::vector<unsigned char> last_pixels;
        check(scene.render_reference(last_pixels,error),error);
        check(last_pixels!=b,"tier 3 swatch changes visible pixels");
        // Independently prepare this one color through the normal full ride
        // pipeline. The optimized bank must bind the same result.
        std::vector<CareerRideSetup> single{variants[0],variants[indices[last]]};
        single.back().garage_paint_slot=0;
        FrontendGpuScene reference;
        check(reference.load_garage(root,1280,720,single,error,nullptr,scene.environment(),scene.car_source()),error);
        check(scene.adopt_garage(reference,error,1),error);
        std::vector<unsigned char> reference_pixels;
        check(scene.render_reference(reference_pixels,error),error);
        check(last_pixels==reference_pixels,"cached palette and independent full ride render agree");
        const auto chosen=menu.entries()[last].part;
        check(menu.accept(shop,profile,purchased,error)&&menu.confirming()&&!purchased,error);
        check(menu.accept(shop,profile,purchased,error)&&purchased&&profile.ride.installed[slot]==chosen,"purchase stores the previewed paint");
        check(menu.prepare_previews(globals,shop,profile,variants,indices,error),error);
        const auto saved=variants[0].request.installed_parts.records[slot];
        check(saved&&db.records.at(*saved).part_name_hash==chosen,"normal ride retains the purchased paint after preview");
        std::cout<<"PASS garage paint car="<<profile.car<<" slot="<<slot<<" rim_level="<<test.wheel_level
            <<" engine_level="<<test.engine_level<<" variants="<<variants.size()<<" resolve_ms="<<resolve_ms<<" prepare_ms="<<prepared_ms<<" same-material, tier 3 and purchase\n";
    }
}
}
