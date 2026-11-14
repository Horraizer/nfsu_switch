// Native menu witnesses:4E3320,4E40D0,4E4C40,4DC230/280,4DBF90.
// Runtime tests follow user input through the actual retail catalog.
namespace {
#include "career_palette_native.inc"
void verify_original_garage(const std::filesystem::path& root,const nfsu::CareerCatalog& catalog) {
    using namespace nfsu;
    const auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    CareerProfile p;CareerGarage menu;bool purchased=false;std::string error;
    if(!catalog.intro_won(p,error)||!catalog.movie_finished(p,error))throw std::runtime_error(error);
    const auto starter=std::find_if(catalog.starters().begin(),catalog.starters().end(),[](const auto& car){return car.name=="CIVIC";});
    check(starter!=catalog.starters().end(),"garage regression starter");
    if(!catalog.buy(p,std::size_t(starter-catalog.starters().begin()),error)||!catalog.movie_finished(p,error))throw std::runtime_error(error);
    p.page=CareerPage::garage;p.customization_seen=true;p.bank=1000000;
    const auto introductory=p;
    const auto enter=[&]{if(!menu.accept(catalog,p,purchased,error))throw std::runtime_error(error);};
    const auto category=[&](unsigned at){
        menu.reset(catalog,p);enter();
        check(menu.fng()==0xb7e48c,"native Visual submenu uses MU_GaragePartsChooseVisual");
        menu.move(int(at));enter();
    };
    for(unsigned at:{0U,2U,3U,4U}) {
        category(at);
        for(unsigned i=0;i<menu.entries().size();++i){menu.move(int(i)-int(menu.selected()));check(menu.focus_slot()==0,"category hover keeps whole-car camera");}
    }
    category(5);enter();
    check(menu.entries().size()==5&&menu.entries()[0].available&&menu.entries()[1].available&&
        !menu.entries()[3].available,"career exposes locked body vinyls alongside both normal groups");
    enter();
    const auto locked_family=std::find_if(menu.entries().begin(),menu.entries().end(),
        [](const auto& family){return !family.available;});
    check(menu.entries().size()==9&&locked_family!=menu.entries().end(),
        "career retains unavailable side families in original order");
    menu.move(int(locked_family-menu.entries().begin()));
    check(!menu.accept(catalog,p,purchased,error)&&menu.page()==GaragePage::vinyl_types&&!purchased,
        "locked family cannot open the design browser");
    category(0);enter();check(menu.focus_slot()==1,"accept front bumper starts its camera transition");
    check(!menu.back(catalog,p)&&menu.focus_slot()==0,"leaving parts restores whole-car camera");
    menu.move(1);check(menu.focus_slot()==0,"highlight rear bumper does not turn camera");enter();check(menu.focus_slot()==2,"enter rear bumper turns camera");
    category(2);check(menu.entries()[0].name=="Neon"&&menu.entries()[1].name=="Headlights"&&menu.entries()[2].name=="Taillights","individual accessory headings");
    category(3);enter();
    check(menu.entries().size()==60&&menu.color_family()==0x02DAAB07U,"authored gloss palette");
    for(unsigned i=0;i<60;++i)check(menu.entries()[i].value==i,"level1/2/3 map to authored grid cells");
    menu.move(-int(menu.selected()));const auto durable=p.ride.installed;const auto bank=p.bank;
    check(menu.preview(p).ride.installed[30]==menu.entries()[0].part,"color preview applies without accept");
    FrontendArt art;
    if(!art.load(root,error,false)||!art.load_career_pages(root,catalog,error))throw std::runtime_error(error);
    FrontendUiFrame first_cursor,next_cursor;art.record_career(1280,720,catalog,p,0,"",first_cursor,&menu);
    menu.move_color(1,0);check(menu.entries()[menu.selected()].value==1,"right selects adjacent swatch");
    art.record_career(1280,720,catalog,p,0,"",next_cursor,&menu);
    check(first_cursor.vertices.size()==next_cursor.vertices.size(),"swatch move retains authored geometry");
    unsigned moved=0;
    for(unsigned i=0;i<first_cursor.vertices.size();i+=4) {
        bool changed=false;
        for(unsigned k=0;k<4;++k)changed=changed||first_cursor.vertices[i+k].px!=next_cursor.vertices[i+k].px||first_cursor.vertices[i+k].py!=next_cursor.vertices[i+k].py;
        if(changed) {
            ++moved;for(unsigned k=0;k<4;++k)check(std::abs(next_cursor.vertices[i+k].px-first_cursor.vertices[i+k].px-26.F)<.001F&&
                std::abs(next_cursor.vertices[i+k].py-first_cursor.vertices[i+k].py)<.001F,"only Cursor moves to authored adjacent swatch");
        }
    }
    check(moved==1,"Cursor moves once; fixed CurrentColor arrows stay fixed");
    check(menu.preview(p).ride.installed[30]==menu.entries()[1].part&&p.ride.installed==durable&&p.bank==bank,"browse colors changes preview only");
    menu.move_color(0,1);check(menu.entries()[menu.selected()].value==11,"down retains column");
    if(!menu.entries()[menu.selected()].available)
        check(menu.preview(p).ride.installed[30]==menu.entries()[1].part,"locked swatch retains previous live preview");
    menu.move_color(0,-1);check(menu.entries()[menu.selected()].value==1,"up restores same column");
    for(unsigned i=0;i<20;++i)menu.move_color(1,0);
    check(menu.entries()[menu.selected()].value==9,"right edge cannot wrap or lose cursor");
    for(unsigned i=0;i<20;++i)menu.move_color(-1,0);
    check(menu.entries()[menu.selected()].value==0,"left edge retains visible selection");
    // Finish availability is the original campaign reward gate, not a UI guess.
    for(unsigned event=0;event<256;++event)if(find_race_event(catalog.events(),event))p.status[event]=kEventStatusWon;
    p.style_high_water=1000000;
    for(unsigned field:{1U,8U}) {
        CareerGarage performance;
        performance.reset(catalog,p);performance.move(1);
        check(performance.accept(catalog,p,purchased,error),"enter performance categories");
        const auto& categories=performance.entries();
        const auto row=std::find_if(categories.begin(),categories.end(),[&](const auto& e){return e.value==field;});
        check(row!=categories.end(),"visual performance package category exists");
        performance.move(int(row-categories.begin())-int(performance.selected()));
        check(performance.accept(catalog,p,purchased,error)&&performance.previewing(),"package opens live preview");
        std::vector<unsigned char> globals;
        std::vector<RaceTableIssue> issues;
        check(load_race_image(root/"Global/GLOBALB.BUN",globals,issues),"read retail garage parts");
        std::vector<CareerRideSetup> variants;
        std::vector<unsigned> indices;
        check(performance.prepare_previews(globals,catalog,p,variants,indices,error),error.c_str());
        check(indices.size()>1&&indices[1]>0,"street package has a resident preview");
        const auto& stock=variants.front();
        const auto& street=variants.at(indices[1]);
        if(field==1)check(stock.performance.suspension==0&&street.performance.suspension==1,"preview stages suspension without purchasing");
        else check(stock.request.installed_parts.records[16]!=street.request.installed_parts.records[16],"brake preview swaps the calliper model");
    }
    menu.change_finish(1,catalog,p);check(menu.color_family()==0x03437A52U,"earned metallic finish appears");
    check(menu.entries().size()==30&&menu.entries()[0].value==10,"metallic retains absent level1 row");
    menu.move_color(0,-1);check(menu.entries()[menu.selected()].value==10,"missing metallic row cannot hide cursor");
    menu.change_finish(1,catalog,p);check(menu.color_family()==0x03797533U&&menu.entries()[0].value==30,"earned pearl begins on level3 row");
    menu.move_color(0,-1);check(menu.entries()[menu.selected()].value==30,"two missing rows preserve pearl cursor");
    menu.change_finish(1,catalog,p);check(menu.color_family()==0x02DAAB07U,"finish list wraps");
    for(unsigned i=0;i<std::size(kNativeVinylIcons);++i)check(career_vinyl_icon(i)==kNativeVinylIcons[i],"vinyl icon matches original runtime initializer");
    unsigned family=0;
    for(const auto& row:kNativePaletteNavigation) {
        while(family!=row[0]){menu.change_finish(1,catalog,p);family=(family+1)%3;}
        const auto start=std::find_if(menu.entries().begin(),menu.entries().end(),[&](const auto& e){return e.value==row[1];});
        check(start!=menu.entries().end(),"native palette start cell exists");
        menu.move(int(start-menu.entries().begin())-int(menu.selected()));
        menu.move_color(row[2]==0?-1:row[2]==1?1:0,row[2]==2?-1:row[2]==3?1:0);
        check(menu.entries()[menu.selected()].value==row[3],"palette navigation matches original x86 result");
    }
    //4DAFA0 saves one cursor per finish instead of returning to cell0.
    while(menu.color_family()!=0x02DAAB07U)menu.change_finish(1,catalog,p);
    menu.move(17-int(menu.selected()));
    menu.change_finish(1,catalog,p);menu.move(4-int(menu.selected()));
    const auto metallic_cell=menu.entries()[menu.selected()].value;
    menu.change_finish(1,catalog,p);menu.move(6-int(menu.selected()));
    const auto pearl_cell=menu.entries()[menu.selected()].value;
    menu.change_finish(1,catalog,p);check(menu.entries()[menu.selected()].value==17,"returning to gloss restores its cursor");
    menu.change_finish(1,catalog,p);check(menu.entries()[menu.selected()].value==metallic_cell,"metallic remembers a separate cursor");
    menu.change_finish(1,catalog,p);check(menu.entries()[menu.selected()].value==pearl_cell,"pearl remembers a separate cursor");
    for(const auto& part:catalog.tuning().parts().records)if(part.slot==31&&part.byte_12==17) {
        const auto& t=catalog.tuning();const bool compatible=part.part_name_hash==bin_hash(p.car+"_BODY_1")||
            part.part_name_hash==bin_hash(p.car+"_BODY_2")||part.part_name_hash==bin_hash(p.car+"_BODY_3")||
            part.part_name_hash==bin_hash(p.car+"_BODY_4")||part.part_name_hash==bin_hash(p.car+"_BODY_5");
        check(bool(t.find_part(p.car,34,part.part_name_hash))==compatible,"native body-vinyl car match");
        const auto number=part.name.substr(5);const auto key=bin_hash("ALLCARS_BODY_"+number);
        for(const auto& reward:t.style_rewards())if(reward.kind==2&&reward.key==key) {
            check(reward.points&& !t.part_available(part,p.status,reward.points-1)&&t.part_available(part,p.status,reward.points),"native shared body-vinyl style threshold");
        }
    }
    category(5);check(menu.entries()[0].value==34&&menu.entries()[3].value==31,"native visual layer order is34..31");
    enter();check(menu.page()==GaragePage::vinyl_groups&&menu.entries()[0].name=="Side","layer enters group directly");
    enter();check(menu.page()==GaragePage::vinyl_types,"side group enters family browser");
    constexpr unsigned expected[]={0,2,4,5,7,9,11,12,14};
    check(menu.entries().size()==9,"earned side families present");
    for(unsigned i=0;i<9;++i)check(menu.entries()[i].value==expected[i],"native side family order");
    enter();check(menu.page()==GaragePage::parts&&menu.slot()==34&&menu.fng()==0xb7828c,"family enters original vinyl-design page");
    const auto& tuning=catalog.tuning();
    const auto attr=[&](const CarPartRecord& part,const char* name){
        for(int i=0;i<part.attribute_count;++i){const auto& a=tuning.parts().attributes[part.attribute_index+unsigned(i)];if(a.key==bin_hash(name))return a.value;}return 0U;
    };
    const auto design=std::find_if(tuning.parts().records.begin(),tuning.parts().records.end(),[&](const auto& part){
        return part.slot==31&&part.byte_12==0&&tuning.part_available(part,p.status,p.style_high_water)&&attr(part,"NUMREMAPCOLOURS")>=2;
    });
    check(design!=tuning.parts().records.end(),"original multi-channel vinyl witness");
    if(!catalog.buy_part(p,34,design->part_name_hash,error))throw std::runtime_error("vinyl witness purchase: "+error);
    category(5);const auto before=p.ride.installed;
    check(menu.edit_vinyl_colors(catalog,p,error)&&menu.slot()==54&&menu.color_channels()==attr(*design,"NUMREMAPCOLOURS"),"color action uses NUMREMAPCOLOURS and selected layer");
    std::array<unsigned,4> chosen{};
    for(unsigned channel=0;channel<menu.color_channels();++channel) {
        check(menu.color_channel()==channel&&menu.slot()==54+channel,"native channel slot progression");
        menu.move(-int(menu.selected()));menu.move_color(int(channel+1),0);
        chosen[channel]=menu.entries()[menu.selected()].part;
        const auto preview=menu.preview(p);
        for(unsigned prior=0;prior<=channel;++prior)check(preview.ride.installed[54+prior]==chosen[prior],"multi-channel live color preview retained");
        enter();check(!purchased&&p.ride.installed==before,"color steps do not mutate durable ride");
    }
    check(menu.confirming(),"last channel opens single confirmation");
    check(!menu.back(catalog,p)&&!menu.confirming(),"cancel confirmation retains editable preview");
    const auto last_channel=menu.color_channel();
    menu.move(-int(menu.selected()));
    const auto last_color=menu.entries()[menu.selected()].part;
    const auto last_cell=menu.entries()[menu.selected()].value;
    check(last_channel>0&&!menu.back(catalog,p)&&menu.color_channel()+1==last_channel,"back revisits the previous vinyl channel");
    check(menu.preview(p).ride.installed[54+last_channel]==last_color,"back preserves the later channel draft in live preview");
    enter();
    check(menu.color_channel()==last_channel&&menu.entries()[menu.selected()].value==last_cell&&
        menu.preview(p).ride.installed[54+last_channel]==last_color,"forward restores the later channel cursor and color");
    for(unsigned channel=menu.color_channel();channel;--channel)check(!menu.back(catalog,p)&&menu.color_channel()==channel-1,"back returns to previous color channel");
    check(!menu.back(catalog,p)&&menu.page()==GaragePage::vinyl_layers&&p.ride.installed==before,"back from first channel cancels all draft colors");
    check(menu.edit_vinyl_colors(catalog,p,error),error.c_str());
    const auto channels=menu.color_channels();
    for(unsigned i=0;i<channels;++i)enter();
    enter();check(purchased,"all vinyl channels commit once confirmed");
    for(unsigned i=0;i<channels;++i)check(p.ride.installed[54+i]!=0,"confirmed channel persisted");
    {
        auto early=introductory;early.style_high_water=1000000;
        const auto remappable=std::find_if(tuning.parts().records.begin(),tuning.parts().records.end(),[&](const auto& part){
            return part.slot==31&&part.byte_12==0&&tuning.find_part(early.car,34,part.part_name_hash)==&part&&
                tuning.part_available(part,early.status,early.style_high_water)&&attr(part,"NUMREMAPCOLOURS")>=2;
        });
        check(remappable!=tuning.parts().records.end(),"early career can earn a multi-channel vinyl before later paint tiers");
        check(catalog.buy_part(early,34,remappable->part_name_hash,error),error.c_str());
        CareerGarage locked;locked.reset(catalog,early);
        check(locked.accept(catalog,early,purchased,error),error.c_str());locked.move(5);
        check(locked.accept(catalog,early,purchased,error)&&locked.edit_vinyl_colors(catalog,early,error),error.c_str());
        locked.move(1-int(locked.selected()));
        check(locked.accept(catalog,early,purchased,error)&&locked.color_channel()==1,"early editor advances to its second channel");
        locked.move(2-int(locked.selected()));
        const auto retained=locked.preview(early).ride.installed[55];
        const auto unavailable=std::find_if(locked.entries().begin(),locked.entries().end(),[](const auto& e){return !e.available;});
        check(unavailable!=locked.entries().end(),"later paint tier remains locked in early career");
        locked.move(int(unavailable-locked.entries().begin())-int(locked.selected()));
        check(!locked.back(catalog,early)&&locked.color_channel()==0,"back from a locked second-channel swatch");
        check(locked.accept(catalog,early,purchased,error)&&!locked.entries()[locked.selected()].available,
            "return restores the locked cursor cell");
        check(locked.preview(early).ride.installed[55]==retained,"locked restored cursor keeps the last valid channel preview");
    }
    category(5);enter();
    check(menu.page()==GaragePage::vinyl_layers&&menu.dialog()==GarageDialog::vinyl_layer,"occupied layer opens a modal over the same layer selector");
    check(!menu.back(catalog,p)&&menu.page()==GaragePage::vinyl_layers,"cancel keeps occupied layer");
    const auto painted=p.ride.installed;
    check(menu.reorder_vinyl(1,catalog,p)&&p.ride.installed[33]==painted[34]&&p.ride.installed[34]==0,
        "reorder moves the vinyl to the neighboring layer");
    check(menu.selected()==0&&menu.entries()[menu.selected()].value==34,"native reorder keeps focus on the selected layer");
    for(unsigned i=0;i<4;++i)check(p.ride.installed[50+i]==painted[54+i],"reorder carries the matching colors");
    menu.move(1);enter();menu.move(1);enter();
    check(purchased&&p.ride.installed[33]==0&&p.ride.installed[50]==0,"remove clears design and remap colors");

    // Native4E4610/4E4840 gate groups with a dialog; cancelling never edits
    // the ride.4E56B0 removes duplicate design hashes without clearing colors.
    {
        const auto saved=p;
        p.style_high_water=1000000;
        check(catalog.buy_part(p,34,design->part_name_hash,error),error.c_str());
        const auto old_colors=p.ride.installed[54];
        check(catalog.buy_part(p,33,design->part_name_hash,error),error.c_str());
        check(!p.ride.installed[34]&&p.ride.installed[33]==design->part_name_hash&&p.ride.installed[54]==old_colors,
            "native duplicate-design installation moves only the matching other layer");
        category(5);enter();
        const auto select_group=[&](unsigned value) {
            const auto it=std::find_if(menu.entries().begin(),menu.entries().end(),[&](const auto& e){return e.value==value;});
            check(it!=menu.entries().end(),"requested native vinyl group is visible");
            menu.move(int(it-menu.entries().begin())-int(menu.selected()));
        };
        select_group(4);const auto before_unique=p.ride.installed;enter();
        check(menu.page()==GaragePage::vinyl_groups&&menu.dialog()==GarageDialog::remove_vinyls,
            "Unique group asks to clear layers without opening a new browser");
        menu.move(1);enter();check(!purchased&&p.ride.installed==before_unique,"Cancel button preserves all existing layers");
        enter();check(!menu.back(catalog,p)&&p.ride.installed==before_unique,"B cancels Unique clearing without mutation");
        enter();enter();check(purchased&&menu.page()==GaragePage::vinyl_groups,"native clear-all response stays on group selector");
        for(unsigned layer=31;layer<=34;++layer)check(!p.ride.installed[layer],"Unique preparation clears every design");
        enter();check(menu.page()==GaragePage::parts,"Unique skips the family browser");
        const auto available=std::find_if(menu.entries().begin(),menu.entries().end(),[](const auto& e){return e.available;});
        check(available!=menu.entries().end(),"earned original Unique design exists");
        menu.move(int(available-menu.entries().begin())-int(menu.selected()));enter();enter();
        check(purchased&&p.ride.installed[34],"Unique installs on chosen layer");
        const auto unique_ride=p.ride.installed;const auto bank=p.bank;
        enter();check(menu.dialog()==GarageDialog::already_installed&&!purchased,"installed design shows information instead of charging again");
        enter();check(p.bank==bank&&p.ride.installed==unique_ride,"already-installed acknowledgement is read-only");
        category(5);enter();
        const auto layer_selection=menu.selected();menu.move(1);
        check(menu.selected()==layer_selection&&menu.dialog_selected()==1,"modal input cannot change the selected layer");
        check(!menu.back(catalog,p)&&p.ride.installed==unique_ride,"cancel layer action keeps its ride");
        enter();enter();select_group(0);enter();
        check(menu.dialog()==GarageDialog::remove_unique,"ordinary group confirms removing the exclusive Unique");
        check(!menu.back(catalog,p)&&p.ride.installed==unique_ride,"cancel ordinary replacement retains Unique");
        enter();enter();enter();check(menu.page()==GaragePage::vinyl_types,"confirmed replacement resumes normal family selection");
        const auto hood=std::find_if(tuning.parts().records.begin(),tuning.parts().records.end(),[&](const auto& part){
            return part.slot==8&&part.upgrade_level==1&&tuning.find_part(p.car,8,part.part_name_hash)==&part;
        });
        check(hood!=tuning.parts().records.end()&&catalog.buy_part(p,8,hood->part_name_hash,error),"install custom hood witness");
        category(5);enter();select_group(1);enter();
        check(menu.dialog()==GarageDialog::stock_hood,"hood group asks to restore the stock hood");
        check(!menu.back(catalog,p)&&p.ride.installed[8]==hood->part_name_hash,"cancelling hood warning keeps custom hood");
        enter();enter();check(purchased&&!p.ride.installed[8],"hood confirmation restores stock without selling inventory");
        enter();check(menu.page()==GaragePage::vinyl_types,"hood family browser opens after the conflict is resolved");
        p=saved;
    }

    category(4);check(menu.entries()[0].icon==bin_hash("DECAL_ZONE_WINDSHIELD"),"native decal zone art");
    enter();check(menu.page()==GaragePage::decals_brands&&menu.slot()==67,"windshield opens the sponsor selector");
    check(menu.entries().size()>1,"retail sponsor records");
    menu.move(1);
    const auto white=menu.entries()[menu.selected()].part;
    check(menu.white_decal(),"native sponsor starts white");
    const auto sponsor=menu.entries()[menu.selected()].value;
    menu.toggle_decal_color(catalog,p);
    const auto black=menu.entries()[menu.selected()].part;
    check(menu.entries()[menu.selected()].value==sponsor&&black!=white&&!menu.white_decal(),
        "brand color switches in place");
    menu.move(-1);menu.move(1);
    check(menu.entries()[menu.selected()].part==black&&!menu.white_decal(),"returning to the brand retains its color");
    check(menu.preview(p).ride.installed[67]==black&&!p.ride.installed[67],"hover previews a decal before purchase");
    enter();enter();check(purchased&&p.ride.installed[67]==black,"sponsor selector commits the chosen color");
    category(4);enter();
    check(menu.entries()[menu.selected()].part==black&&!menu.white_decal(),"reopening uses the installed sponsor color");
    category(4);menu.move(2);enter();
    check(menu.page()==GaragePage::decals_slots&&menu.entries().size()==6&&
        menu.entries()[0].icon==bin_hash("DECAL_LEFTDOOR_SLOT1"),"stock door goes directly to six positions");
    category(4);menu.move(6);enter();
    check(menu.page()==GaragePage::decals_sizes&&menu.entries().size()==2,"stock hood offers two layouts");
    enter();check(menu.entries().size()==4,"small hood layout has four positions");
    category(4);menu.move(6);enter();menu.move(1);enter();
    check(menu.entries().size()==8,"large hood layout has eight positions");
    const auto* fitment=tuning.decal_fitment(bin_hash("CIVIC"));
    check(fitment&&(*fitment)[0]==5&&(*fitment)[1]==4,"native Civic widebody fitment row");
    const auto kit=std::find_if(tuning.parts().records.begin(),tuning.parts().records.end(),[&](const auto& part){
        return part.slot==6&&part.name=="MANTIS"&&part.car_type_hash==bin_hash(p.car);
    });
    check(kit!=tuning.parts().records.end(),"Civic Mantis record");
    p.ride.installed[6]=kit->part_name_hash;
    category(4);menu.move(2);enter();
    check(menu.page()==GaragePage::decals_slots&&menu.entries().size()==4&&
        menu.entries()[0].icon==bin_hash("DECAL_LEFTDOOR_SLOT3_WIDEBODY"),"Mantis door positions follow the original car table");
    category(4);menu.move(4);enter();
    check(menu.page()==GaragePage::decals_brands&&menu.slot()==99,"wide quarter goes straight to sponsor selection");
    p.ride.installed[6]=0;

    // Actual hover must predict both upgrades and reinstallation of stock.
    menu.reset(catalog,p);menu.move(1);enter();enter();
    check(menu.page()==GaragePage::packages,"actual engine package picker");
    const auto durable_performance=p.ride.performance;const auto preview_bank=p.bank;
    menu.move(1-int(menu.selected()));
    const auto upgrade=menu.preview(p);
    check(upgrade.ride.performance[2]==1&&p.ride.performance==durable_performance&&p.bank==preview_bank,"package hover stages only hypothetical engine");
    std::array<float,3> up{},down{};
    check(catalog.performance_ratings(upgrade,up),"predicted upgraded ratings");
    menu.move(-int(menu.selected()));
    check(menu.preview(p).ride.performance[2]==0&&catalog.performance_ratings(menu.preview(p),down)&&up[0]>down[0],"stock hover predicts lower acceleration");
    menu.move(1);FrontendUiFrame predicted;art.record_career(1280,720,catalog,p,0,"",predicted,&menu);
    check(!predicted.vertices.empty(),"package comparison and original content produce actual UI commands");
    check(catalog.buy_performance(p,2,1,error)&&catalog.buy_performance(p,8,1,error),error.c_str());
    category(3);menu.move(3);enter();check(menu.page()==GaragePage::colors&&menu.slot()==37&&menu.focus_slot()==37,"native engine performance package permits engine accent paint");
    category(3);menu.move(4);enter();check(menu.page()==GaragePage::colors&&menu.slot()==39&&menu.focus_slot()==39,"native brake package permits caliper paint");
    // Trade In follows the same original reward gate as its car roster.
    CareerProfile original=p;menu.reset(catalog,p);menu.move(2);enter();
    check(menu.page()==GaragePage::trade_in&&!menu.entries().empty(),"Trade In enters authored car roster");
    const auto first_signature=menu.preview_signature(p);const auto selected_car=menu.preview(p).car;
    if(menu.entries().size()>1){menu.move(1);check(menu.preview_signature(p)==first_signature,"trade highlight keeps resident bank identity");menu.move(-1);}
    const auto trade_cost=menu.entries()[menu.selected()].trade_cost;
    check(p.car==original.car&&p.bank==original.bank,"trade highlight preserves owned car and bank");
    enter();check(menu.trade_review()==1&&!menu.confirming()&&!purchased,"trade shows owned car performance first");
    enter();check(menu.trade_review()==2&&!menu.confirming()&&!purchased,"trade shows replacement performance second");
    enter();check(menu.confirming()&&!purchased,"trade confirmation does not spend");
    enter();check(purchased&&p.car==selected_car&&std::int64_t(p.bank)==std::int64_t(original.bank)-trade_cost,"confirmed trade commits signed original credit");
    check(p.ride.performance==original.ride.performance&&p.ride.unique==original.ride.unique&&catalog.valid(p,error),"trade retains earned performance and valid mapped visual parts");
    const auto save=std::filesystem::temp_directory_path()/("nfsu-trade-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    check(save_career(save,catalog,p,error),error.c_str());CareerProfile restored;
    check(load_career(save,catalog,restored,error)&&restored.car==p.car&&restored.ride.installed==p.ride.installed,"traded car round trips through current save schema");
    for(unsigned n=0;n<2;++n){auto file=save;file+="."+std::to_string(n);std::filesystem::remove(file);}
    std::cout<<"PASS native garage entry camera timing, palette grid/live preview, earned finishes and layered vinyl flow/colors\n";
}
}
