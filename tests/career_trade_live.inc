// Actual resident Trade In selection, animation, UI and transaction boundary.
namespace {
void verify_trade_garage(const std::filesystem::path& root,const std::filesystem::path& output,
                         const nfsu::CareerCatalog& catalog) {
    using namespace nfsu;
    const auto check=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};
    check(!std::filesystem::exists(output),"fresh trade output required");std::filesystem::create_directories(output);
    CareerProfile profile;std::string error;bool purchased=false;
    check(catalog.intro_won(profile,error)&&catalog.movie_finished(profile,error),error);
    const auto civic=std::find_if(catalog.starters().begin(),catalog.starters().end(),[](const auto& c){return c.name=="CIVIC";});
    check(civic!=catalog.starters().end(),"Civic starter present");
    check(catalog.buy(profile,std::size_t(civic-catalog.starters().begin()),error)&&catalog.movie_finished(profile,error),error);
    profile.page=CareerPage::garage;profile.bank=100000;profile.customization_seen=true;
    if(std::getenv("NFSU_TRADE_ALL_CARS"))for(const auto& event:catalog.events().records)
        if(event.number<=kCareerLastEvent)profile.status[event.number]=kEventStatusWon;
    if(std::getenv("NFSU_TRADE_TUNED")) {
        for(unsigned field:{2U,6U,7U})check(catalog.buy_performance(profile,field,1,error),error);
        for(unsigned slot:{1U,17U,116U}) {
            const auto& parts=catalog.tuning().parts().records;
            const auto part=std::find_if(parts.begin(),parts.end(),[&](const auto& item){
                return unsigned(item.slot)==slot&&item.upgrade_level==1&&
                    catalog.tuning().find_part(profile.car,slot,item.part_name_hash)==&item;
            });
            check(part!=parts.end(),"tuned trade source part present");
            check(catalog.buy_part(profile,slot,part->part_name_hash,error),error);
        }
    }
    CareerGarage menu;menu.reset(catalog,profile);menu.move(2);
    check(menu.accept(catalog,profile,purchased,error)&&menu.page()==GaragePage::trade_in,error);
    const auto original=profile;const auto signature=menu.preview_signature(profile);
    check(menu.entries().size()>1,"multiple unlocked Trade In candidates");
    std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;
    check(load_race_image(root/"Global/GLOBALB.BUN",globals,issues),"read-only trade GLOBALB");
    std::vector<CareerRideSetup> variants;std::vector<unsigned> indices;
    check(menu.prepare_previews(globals,catalog,profile,variants,indices,error),error);
    check(variants.size()==menu.entries().size()+1,"resident trade bank includes every unlocked candidate");
    FrontendGpuScene visible,prepared;
    check(visible.load_garage(root,1280,720,{variants.front()},error),error);
    visible.focus_part(menu.focus_slot());
    visible.prepare_car_change();
    for(unsigned i=0;i<120;++i)check(visible.frame(1.F/60.F,i*67,error),error);
    check(!visible.car_transitioning()&&visible.selected_car_type()==profile.car,
          "camera preparation retains the car while bank data is unavailable");
    check(prepared.load_garage(root,1280,720,variants,error,nullptr,visible.environment(),visible.car_source()),error);
    check(visible.adopt_garage(prepared,error),error);
    FrontendArt art;check(art.load(root,error,false)&&art.load_career_pages(root,catalog,error),error);
    check(art.atlas().width<=4096&&art.atlas().height<=4096&&art.atlas().rgba.size()<=2048ULL*4096*4,
          "all original trade icons/logos fit the unchanged UI byte ceiling");
    {
        auto starter=profile;starter.page=CareerPage::choose_car;
        std::string previous;
        for(unsigned i=0;i<catalog.starters().size();++i) {
            std::vector<unsigned char> pixels(1280*720*4);
            art.draw_career({pixels.data(),1280,720,1280*4},catalog,starter,i,"");
            Sha256 row;std::size_t lit=0;
            for(unsigned y=0;y<140;++y) {
                row.update(pixels.data()+y*1280*4,1280*4);
                for(unsigned x=0;x<1280;++x)lit+=pixels[(y*1280+x)*4+3]!=0;
            }
            const auto current=sha256_hex(row.finish());
            check(lit>0&&current!=previous,"starter manufacturer browser draws and changes its top row");
            previous=current;
            if(i==0) {
                std::ofstream image(output/"starter-ui.ppm",std::ios::binary);image<<"P6\n1280 720\n255\n";
                for(std::size_t at=0;at<pixels.size();at+=4)image.write(reinterpret_cast<const char*>(pixels.data()+at),3);
                check(bool(image),"starter UI capture write");
            }
        }
    }
    std::cout<<"TRADE_ATLAS "<<art.atlas().width<<'x'<<art.atlas().height<<" bytes="<<art.atlas().rgba.size()<<'\n';
    const auto& bytes=visible.commands().resource_table().totals;
    std::cout<<"TRADE_BANK cars="<<variants.size()<<" vertex_bytes="<<bytes.vertex_bytes
             <<" index_bytes="<<bytes.index_bytes<<" texture_bytes="<<bytes.texture_bytes<<'\n';
    unsigned tick=8000;std::string previous_ui,previous_logo;
    const auto travel=[&] {
        const auto& draws=visible.commands().owning_draws();
        const auto body=std::find_if(draws.begin(),draws.end(),[](const auto& draw){
            return draw.name.find("car group ")!=std::string::npos&&draw.name.find("_FE_")==std::string::npos;
        });
        check(body!=draws.end(),"visible car body");
        return body->placement.rows[12];
    };
    for(unsigned i=0;i<menu.entries().size();++i) {
        menu.move(int(i)-int(menu.selected()));const auto target=menu.preview(profile);
        const auto car=std::find_if(catalog.cars().begin(),catalog.cars().end(),[&](const auto& c){return c.name==target.car;});
        check(car!=catalog.cars().end()&&menu.entries()[i].icon==bin_hash("CARSELECT_MANUFACTURER_"+car->brand),
              "native4C3240 manufacturer thumbnail binding");
        check(menu.preview_signature(profile)==signature,"highlight never invalidates the resident trade bank");
        check(indices[i]>0&&variants[indices[i]].request.car_type_name==target.car,"selected menu row maps to its own car");
        const auto parked=travel();
        check(visible.select_car(indices[i],error),error);unsigned frames=0,departure=0;
        do {
            visible.focus_part(menu.focus_slot());visible.road_target(menu.road_target());
            check(visible.select_car(indices[i],error),error);
            check(visible.frame(1.F/60.F,++tick*4000/60,error),error);
            ++frames;
            if(!departure&&std::abs(travel()-parked)>.0001F)departure=frames;
        }while(visible.car_transitioning()&&frames<900);
        check(!visible.car_transitioning()&&visible.selected_car_type()==target.car,
              "native car travel must complete at the selected visible car");
        check(departure&&(i!=0||departure<=3),
              "Trade In bank publication must not restart a completed camera preparation");
        check(validate_recorded_commands(visible.commands(),error),error);
        FrontendUiFrame ui;art.record_career(1280,720,catalog,profile,0,"",ui,&menu);
        const auto ui_hash=sha256_hex(sha256(reinterpret_cast<const unsigned char*>(ui.vertices.data()),ui.vertices.size()*sizeof(ui.vertices[0])));
        check(!ui.vertices.empty()&&ui_hash!=previous_ui,"trade car name, cost, logo and data change with selection");previous_ui=ui_hash;
        // Native Trade In SecondaryCarLogo is separate from the top scroller.
        // Rasterize only UI so the rotating car cannot fake a logo change.
        std::vector<unsigned char> ui_pixels(1280*720*4);
        art.draw_career({ui_pixels.data(),1280,720,1280*4},catalog,profile,0,"",&menu);
        Sha256 logo;
        for(unsigned y=142;y<238;++y)logo.update(ui_pixels.data()+(y*1280+800)*4,480*4);
        const auto logo_hash=sha256_hex(logo.finish());
        check(logo_hash!=previous_logo,"selected car logo replaces the static FNG sample image");previous_logo=logo_hash;
        check(profile.car==original.car&&profile.bank==original.bank,"hover leaves owned car and bank intact");
        if(i<2) {
            std::vector<unsigned char> pixels;check(visible.render_reference(pixels,error),error);
            art.draw_career({pixels.data(),1280,720,1280*4},catalog,profile,0,"",&menu);
            std::ofstream image(output/("trade-"+std::to_string(i)+".ppm"),std::ios::binary);image<<"P6\n1280 720\n255\n";
            for(std::size_t j=0;j<pixels.size();j+=4)image.write(reinterpret_cast<const char*>(pixels.data()+j),3);
            check(bool(image),"trade frame written");
        }
        std::cout<<"TRADE_VISIBLE index="<<i<<" car="<<target.car<<" frames="<<frames<<" departure_frame="<<departure<<" cost="<<menu.entries()[i].trade_cost<<'\n';
    }
    const auto target=menu.preview(profile).car;const int cost=menu.entries()[menu.selected()].trade_cost;
    for(unsigned page=1;page<=2;++page) {
        check(menu.accept(catalog,profile,purchased,error)&&!purchased&&!menu.confirming()&&
              menu.trade_review()==page&&profile.car==original.car&&profile.bank==original.bank,
              "trade review precedes confirmation and preserves the profile");
    }
    check(menu.accept(catalog,profile,purchased,error)&&!purchased&&menu.confirming(),"trade confirmation is separate");
    check(menu.accept(catalog,profile,purchased,error)&&purchased&&profile.car==target&&
          std::int64_t(profile.bank)==std::int64_t(original.bank)-cost,"confirmed trade installs highlighted car and signed cost");
    std::cout<<"PASS resident Trade In switching, complete car travel, UI bindings and transaction\n";
}
}
