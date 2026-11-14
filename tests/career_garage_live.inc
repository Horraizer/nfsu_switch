// Actual menu -> shared request plan -> worker bank -> GPU command publication.
// Native renderer execution and Switch input scheduling still require hardware.
namespace {
void verify_live_garage(const std::filesystem::path& root,const std::filesystem::path& output,const nfsu::CareerCatalog& catalog) {
    using namespace nfsu;
    const auto check=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};
    check(!std::filesystem::exists(output),"fresh live garage output required");std::filesystem::create_directories(output);
    CareerProfile profile;std::string error;
    check(catalog.intro_won(profile,error)&&catalog.movie_finished(profile,error),error);
    const auto civic=std::find_if(catalog.starters().begin(),catalog.starters().end(),[](const auto& c){return c.name=="CIVIC";});
    check(civic!=catalog.starters().end(),"Civic starter present");
    check(catalog.buy(profile,std::size_t(civic-catalog.starters().begin()),error)&&catalog.movie_finished(profile,error),error);
    profile.page=CareerPage::garage;profile.customization_seen=true;profile.bank=100000;
    std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;
    check(load_race_image(root/"Global/GLOBALB.BUN",globals,issues),"read-only garage global input");
    FrontendGpuScene visible;check(visible.load(root,1280,720,error,profile.car),error);
    FrontendArt art;check(art.load(root,error,false)&&art.load_career_pages(root,catalog,error),error);
    CareerGarage menu;menu.reset(catalog,profile);bool purchased=false;
    std::string resident_signature,resident_base,resident_car;std::vector<unsigned> indices;
    const auto shape=[&] {
        std::ostringstream result;
        for(std::size_t i=0;i<visible.commands().draw_count();++i) {
            const auto& draw=visible.commands().draw_view(i).definition;
            if(draw.name.find("car group ")==std::string::npos&&draw.name.find("neon")==std::string::npos)continue;
            result<<draw.index_count<<':'<<draw.vertex_count<<';';
            // Rim and brake paint changes material uniforms, not geometry or
            // texture payload. Include actual submitted coefficients.
            for(const auto* coefficient:{draw.material.surface.diffuse_min,draw.material.surface.diffuse_range,
                draw.material.surface.specular_min,draw.material.surface.specular_range,
                draw.material.surface.environment_min,draw.material.surface.environment_range})
                for(unsigned channel=0;channel<4;++channel)result<<coefficient[channel]<<';';
            for(auto handle:{draw.vertex_resource,draw.index_resource,draw.texture_resource}) {
                if(handle==kNoHandle)continue;
                const auto& data=visible.commands().resource_table().resources.at(handle);
                result<<sha256_hex(sha256(data.data,std::size_t(data.bytes)))<<';';
            }
            if(draw.name.find("neon")!=std::string::npos)
                result<<draw.material.alpha[0]<<':'<<draw.material.alpha[2]<<':'<<draw.material.environment_color[0]<<':'<<draw.material.environment_color[1]<<':'<<draw.material.environment_color[2];
        }
        return result.str();
    };
    const auto apply=[&] {
        const auto signature=menu.preview_signature(profile);
        const auto installed=CareerGarage::installed_signature(profile);
        const bool reuse=!menu.previewing()&&!resident_signature.empty()&&installed==resident_base;
        visible.focus_part(menu.focus_slot());visible.road_target(menu.road_target());
        if(!reuse&&signature!=resident_signature) {
            std::vector<CareerRideSetup> variants;std::vector<unsigned> next_indices;
            check(menu.prepare_previews(globals,catalog,profile,variants,next_indices,error),error);
            const auto room=visible.environment();const auto source=visible.car_source();FrontendGpuScene prepared;std::string load_error;
            const auto begin=std::chrono::steady_clock::now();
            auto ready=std::async(std::launch::async,[&]{return prepared.load_garage(root,1280,720,variants,load_error,nullptr,room,source);});
            unsigned frames=0;
            while(ready.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) {
                check(visible.frame(0,1000,error),error);++frames;std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(ready.get(),load_error);check(prepared.environment()==room,"all job variants keep shared immutable room");
            if(source&&resident_car==profile.car)check(prepared.car_source()==source,"all menu jobs reuse the immutable parsed car geometry source");
            const unsigned selected=menu.previewing()&&menu.preview_index()<next_indices.size()?next_indices[menu.preview_index()]:0;
            const auto* old=&visible.commands();check(visible.adopt_garage(prepared,error,menu.page()==GaragePage::trade_in?0:selected),error);
            check(&prepared.commands()==old,"old GPU command owner survives publication");
            // Validate both owners before the old renderer would retire.
            check(validate_recorded_commands(prepared.commands(),error)&&validate_recorded_commands(visible.commands(),error),error);
            indices=std::move(next_indices);resident_signature=signature;resident_base=installed;resident_car=profile.car;
            std::cout<<"LIVE BANK page="<<unsigned(menu.page())<<" slot="<<menu.slot()<<" variants="<<variants.size()
                     <<" selected="<<selected<<" prepare_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<" live_frames="<<frames<<"\n";
        }
        const unsigned selected=menu.previewing()&&menu.preview_index()<indices.size()?indices[menu.preview_index()]:0;
        check((menu.page()==GaragePage::trade_in?visible.select_car(selected,error):visible.select_part(selected,error))&&visible.frame(0,1000,error),error);
        FrontendUiFrame ui;art.record_career(1280,720,catalog,profile,0,"",ui,&menu);check(!ui.vertices.empty(),"actual garage UI recorded");
    };
    const auto enter=[&] {check(menu.accept(catalog,profile,purchased,error),error);apply();};
    const auto category=[&](int index) {menu.reset(catalog,profile);apply();enter();menu.move(index);apply();enter();};
    const auto choose_upgrade=[&](bool multicolor=false) {
        const auto& items=menu.entries();auto it=std::find_if(items.begin(),items.end(),[&](const auto& e){
            if(!e.available||!e.part||e.installed)return false;
            if(!multicolor)return true;
            const auto& tuning=catalog.tuning();const auto* part=tuning.find_part(profile.car,menu.slot(),e.part);
            if(!part)return false;
            for(int i=0;i<part->attribute_count;++i) {
                const auto& a=tuning.parts().attributes[part->attribute_index+unsigned(i)];
                if(a.key==bin_hash("NUMREMAPCOLOURS")&&a.value>=2)return true;
            }
            return false;
        });
        check(it!=items.end(),"unlocked noninstalled visual preview exists");menu.move(int(it-items.begin())-int(menu.selected()));apply();
    };
    const auto capture=[&](const char* name) {
        for(unsigned i=0;i<180;++i)check(visible.frame(1.F/60,1000,error),error);
        std::vector<unsigned char> pixels;check(visible.render_reference(pixels,error),error);
        FrontendUiFrame ui;art.record_career(1280,720,catalog,profile,0,"",ui,&menu);
        art.draw_career({pixels.data(),1280,720,1280*4},catalog,profile,0,"",&menu);
        std::ofstream file(output/name,std::ios::binary);file<<"P6\n1280 720\n255\n";
        for(std::size_t i=0;i<pixels.size();i+=4)file.write(reinterpret_cast<const char*>(pixels.data()+i),3);
        check(bool(file),"live garage preview image saved");
    };
    const auto transaction=[&](const char* name,unsigned slot) {
        const auto before=profile.ride.installed;
        check(visible.select_part(0,error)&&visible.frame(0,1000,error),error);
        const auto baseline=shape();apply();choose_upgrade(slot>=31&&slot<=34);
        const auto part=menu.entries()[menu.selected()].part;
        check(profile.ride.installed==before,"highlighting does not install or spend");
        check(shape()!=baseline,"actual menu selection changes GPU geometry/skin/neon payload");
        if(slot==37||slot==39) {
            const auto active=shape();const auto first=menu.selected();
            auto other=std::find_if(menu.entries().begin(),menu.entries().end(),[&](const auto& e){return e.available&&e.part!=part;});
            check(other!=menu.entries().end(),"second special paint swatch present");
            menu.move(int(other-menu.entries().begin())-int(first));apply();
            check(shape()!=active,"second special paint changes actual color payload while geometry stays exposed");
            menu.move(int(first)-int(menu.selected()));apply();
        }
        const auto preview_shape=shape();
        if(slot==116) {
            // A drawable command alone did not prove that any light reached the
            // room pixels. Isolate the actual live scalar at a fixed pose.
            for(unsigned i=0;i<180;++i)check(visible.frame(1.F/60,1000,error),error);
            auto& commands=const_cast<RecordedCommands&>(visible.commands());
            auto neon=std::find_if(commands.owning_draws().begin(),commands.owning_draws().end(),[](const auto& d){return d.name.find("car underbody neon")!=std::string::npos;});
            check(neon!=commands.owning_draws().end()&&neon->material.alpha[3]==7&&neon->material.alpha[0]>0,"live FE neon controller is nonzero");
            std::vector<unsigned char> lit,dark;check(visible.render_reference(lit,error),error);
            const auto alpha=neon->material.alpha[0];neon->material.alpha[0]=0;
            const bool dark_ok=visible.render_reference(dark,error);neon->material.alpha[0]=alpha;
            check(dark_ok,error);check(lit!=dark,"FE neon changes room pixels at identical camera/road/time");
            unsigned changed=0;for(std::size_t i=0;i<lit.size();i+=4)
                if(lit[i]!=dark[i]||lit[i+1]!=dark[i+1]||lit[i+2]!=dark[i+2])++changed;
            std::cout<<"PASS LIVE isolated FE neon contribution pixels="<<changed<<"\n";
        }
        capture(name);enter();check(!purchased&&menu.confirming(),"first accept only opens confirmation");
        enter();check(purchased&&profile.ride.installed[slot]==part,"second accept persists the exact previewed part");
        check(!menu.back(catalog,profile),"back to parent");apply();
        const auto installed=shape();
        if(slot==37||slot==39) {
            check(installed!=preview_shape,"leaving paint restores hood/rim visibility from the native preview");
        } else if(installed!=preview_shape) {
            std::ofstream(output/"preview-shape.txt")<<preview_shape;
            std::ofstream(output/"installed-shape.txt")<<installed;
            capture("live-installed-mismatch.ppm");
        }
        if(slot!=37&&slot!=39)check(installed==preview_shape,"leaving browser displays the purchased variant, not the old base");
        std::cout<<"PASS LIVE preview, confirmation, purchase and parent restoration slot="<<slot<<" part="<<part<<"\n";
    };
    if(!std::getenv("NFSU_GARAGE_SPECIAL_PAINT_ONLY")) {
    category(0);enter();transaction("live-bumper.ppm",1);
    category(1);check(menu.road_target()==1,"native rim manufacturer browser target1 km/h");
    unsigned motion_bits=0;const auto speed=menu.road_target();std::memcpy(&motion_bits,&speed,4);
    check(motion_bits==kNativeGarageMotion[1][1],"original rim target setter bits");enter();
    check(menu.page()==GaragePage::rim_styles&&menu.road_target()==1,"native rim style preview remains slow");
    choose_upgrade();capture("live-rim-style.ppm");enter();
    check(menu.page()==GaragePage::rim_sizes,"rim style accepts into the original size picker");
    transaction("live-rims.ppm",17);
    category(3);menu.move(1);enter();
    check(menu.road_target()==1,"painting installed rims retains slow road motion");
    transaction("live-rim-paint.ppm",41);
    category(2);enter();transaction("live-neon.ppm",116);
    category(2);enter();transaction("live-neon-second.ppm",116);
    category(3);enter();transaction("live-paint.ppm",30);
    const auto native=std::find_if(std::begin(kNativeSpecialVinyl),std::end(kNativeSpecialVinyl),[](const auto& row){return row[0]==15&&row[1]==4&&row[4];});
    check(native!=std::end(kNativeSpecialVinyl),"native earned Unique witness present");
    profile.style_high_water=(*native)[3];
    category(5);enter();check(menu.page()==GaragePage::vinyl_groups,"actual vinyl group page");
    auto unique=std::find_if(menu.entries().begin(),menu.entries().end(),[](const auto& e){return e.name=="Unique";});
    check(unique!=menu.entries().end()&&unique->value==4,"Unique group routes directly to native type15");
    menu.move(int(unique-menu.entries().begin())-int(menu.selected()));enter();
    auto item=std::find_if(menu.entries().begin(),menu.entries().end(),[&](const auto& e){return e.part==(*native)[2];});
    check(item!=menu.entries().end()&&item->available,"earned Unique is available by its original style flag");
    menu.move(int(item-menu.entries().begin())-int(menu.selected()));apply();capture("live-unique.ppm");
    const auto bank=profile.bank;enter();enter();check(purchased&&profile.ride.installed[34]==(*native)[2]&&profile.bank==bank,"earned level4 Unique installs without a false shop price");
    check(catalog.valid(profile,error),error);
    // The introductory tear designs are single-color. Earn the later designs
    // explicitly so the channel revisit below cannot silently skip its work.
    profile.style_high_water=1000000;
    for(const auto& event:catalog.events().records)profile.status[event.number]=kEventStatusWon;
    category(5);enter();
    check(menu.page()==GaragePage::vinyl_layers&&menu.dialog()==GarageDialog::vinyl_layer,"installed Unique opens replace/remove over the layer selector");
    enter();check(menu.page()==GaragePage::vinyl_groups,"replace opens group selector");
    capture("live-replace-unique.ppm");
    const auto unique_ride=profile.ride.installed;
    enter();check(menu.dialog()==GarageDialog::remove_unique,"Side confirms replacing the exclusive Unique");
    capture("live-unique-warning.ppm");
    check(!menu.back(catalog,profile)&&profile.ride.installed==unique_ride,"cancelling Unique replacement retains all layers");
    enter();enter();check(purchased&&menu.page()==GaragePage::vinyl_groups,"confirm clears Unique and stays in its group selector");
    enter();check(menu.page()==GaragePage::vinyl_types,"Side opens family selector");enter();
    check(menu.page()==GaragePage::parts&&menu.slot()==34,"actual Side/family/design path");
    transaction("live-vinyl-design.ppm",34);
    category(5);const auto before_colors=profile.ride.installed;const auto durable_shape=shape();
    check(menu.edit_vinyl_colors(catalog,profile,error),error);apply();
    const auto first_color=menu.preview(profile).ride.installed[54];
    choose_upgrade();menu.move_color(1,0);apply();
    check(menu.preview(profile).ride.installed[54]!=first_color,"palette moves actual vinyl color");
    const auto colored=shape();check(colored!=durable_shape,"live vinyl color reaches composite GPU payload");
    capture("live-vinyl-color.ppm");
    enter();check(!purchased&&profile.ride.installed==before_colors,"vinyl first-channel preview remains uncommitted");
    check(menu.color_channels()>1,"live fixture exercises a multi-channel vinyl");
    {
        check(menu.color_channel()==1,"live editor enters second remap channel");
        menu.move(4-int(menu.selected()));apply();
        const auto second_channel=shape();
        const auto second_cell=menu.entries()[menu.selected()].value;
        check(!menu.back(catalog,profile)&&menu.color_channel()==0,"live editor returns to first remap channel");apply();
        check(shape()==second_channel,"back retains both remap colors in submitted GPU payload");
        enter();check(menu.color_channel()==1&&menu.entries()[menu.selected()].value==second_cell,
            "live editor restores second-channel cursor");
        check(shape()==second_channel,"forward restores the same submitted vinyl composite");
        capture("live-vinyl-channel-revisit.ppm");
    }
    while(menu.page()==GaragePage::colors)check(!menu.back(catalog,profile),"cancel vinyl color picker");
    apply();check(shape()==durable_shape,"cancelling recolor restores actual installed composite");
    }
    for(const auto& event:catalog.events().records)profile.status[event.number]=kEventStatusWon;
    check(catalog.buy_performance(profile,8,1,error),error);
    category(3);menu.move(4);enter();
    check(menu.slot()==39,"actual performance brake opens calliper paint");
    transaction("live-brake-paint.ppm",39);
    check(catalog.buy_performance(profile,2,1,error),error);
    category(3);menu.move(3);enter();
    check(menu.slot()==37,"actual engine package opens engine accent paint");
    transaction("live-engine-paint.ppm",37);
    const auto performance_page=[&](unsigned field) {
        menu.reset(catalog,profile);menu.move(1);enter();
        const auto found=std::find_if(menu.entries().begin(),menu.entries().end(),
            [field](const auto& entry){return entry.value==field;});
        check(found!=menu.entries().end(),"performance category available");
        menu.move(int(found-menu.entries().begin())-int(menu.selected()));enter();
    };
    const auto body_height=[&] {
        const auto& draws=visible.commands().owning_draws();
        const auto draw=std::find_if(draws.begin(),draws.end(),[](const auto& entry){
            return entry.name.find("car group ")!=std::string::npos&&entry.name.find("_FE_")==std::string::npos;
        });
        check(draw!=draws.end(),"body height witness");
        return draw->placement.rows[14];
    };
    performance_page(1);
    const auto original_suspension=profile.ride.performance[1];
    const auto original_height=body_height();
    for(unsigned level=1;level<=3;++level) {
        menu.move(int(level)-int(menu.selected()));apply();
        check(body_height()<original_height&&profile.ride.performance[1]==original_suspension&&
              !visible.car_transitioning(),"each suspension package lowers the visible body on hover");
        if(level==3)capture("live-suspension-extreme.ppm");
    }
    check(!menu.back(catalog,profile),"leave suspension preview");apply();
    check(body_height()==original_height,"cancel restores installed suspension height");
    performance_page(8);
    check(menu.focus_slot()==16&&menu.selected()==profile.ride.performance[8],
          "brake packages open at installed tier and use native caliper camera");
    const auto installed_brake=shape();
    menu.move(1);apply();
    check(shape()!=installed_brake&&!visible.car_transitioning()&&profile.ride.performance[8]==1,
          "highlight changes brake model in place without buying");
    for(unsigned frame=0;frame<120;++frame)check(visible.frame(1.F/60,1000+frame*67,error),error);
    capture("live-brake-package.ppm");
    const auto owned_car=profile.car;const auto owned_bank=profile.bank;
    menu.reset(catalog,profile);menu.move(2);enter();
    check(menu.page()==GaragePage::trade_in&&menu.preview(profile).car!=profile.car,"live Trade In has a different available car");
    const auto trade_preview=shape();capture("live-trade.ppm");
    check(profile.car==owned_car&&profile.bank==owned_bank,"live car preview leaves career save unchanged");
    check(!menu.back(catalog,profile),"cancel Trade In returns to garage");apply();
    menu.move(2-int(menu.selected()));enter();capture("live-trade-confirm.ppm");
    const auto target=menu.preview(profile).car;
    for(unsigned page=1;page<=2;++page) {
        enter();
        check(!purchased&&menu.trade_review()==page&&!menu.confirming(),"live Trade In shows both stat pages");
    }
    enter();
    check(!purchased&&menu.confirming(),"live Trade In shows the separate confirmation");
    enter();
    check(purchased&&profile.car==target&&profile.car!=owned_car&&catalog.valid(profile,error),"live Trade In buys and restores installed new car without loader");
    check(shape()!=trade_preview,"new car is published after purchase");
    menu.reset(catalog,profile);menu.move(1);enter();enter();menu.move(1-int(menu.selected()));apply();capture("live-performance-hover.ppm");
    const auto complete=shape();check(bool(visible.car_source()),"live garage retains parsed source for subsequent jobs");
    visible.release_preparation_cache();check(!visible.car_source(),"career shutdown releases the raw/parsed source lease");
    check(visible.frame(0,1000,error)&&shape()==complete,"released preparation cache cannot invalidate resident GPU payloads");
    std::cout<<"PASS LIVE all visual menus and shared asynchronous publication; original Unique earned gate; source saves untouched\n";
}
}
