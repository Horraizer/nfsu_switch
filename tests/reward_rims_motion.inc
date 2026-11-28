// Earned result -> reward selector -> resident bank -> actual wheel matrices.
// The #47 inspection speed deliberately differs from original PC mode4.
namespace {
void verify_reward_rims_motion(const std::filesystem::path& root,const nfsu::CareerCatalog& catalog,
                              const std::filesystem::path& output={}) {
    using namespace nfsu;
    const auto check=[](bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);};
    std::string error;
    if(!output.empty()){check(!std::filesystem::exists(output),"fresh reward rims output required");std::filesystem::create_directories(output);}
    CareerProfile starter;
    check(catalog.intro_won(starter,error)&&catalog.movie_finished(starter,error),error);
    const auto car=std::find_if(catalog.starters().begin(),catalog.starters().end(),[](const auto& row){return row.name=="CIVIC";});
    check(car!=catalog.starters().end(),"reward-rims starter");
    check(catalog.buy(starter,std::size_t(car-catalog.starters().begin()),error)&&catalog.movie_finished(starter,error),error);
    const auto earn=[&](unsigned event){
        auto p=starter;
        // Saved career before this race; production result handling earns it.
        for(const auto& prior:catalog.events().records)if(prior.number<event)p.status[prior.number]=kEventStatusWon;
        p.selected_event=event;p.page=CareerPage::briefing;
        check(catalog.event_finished(p,event,1,error),error);
        check(p.page==CareerPage::unique_reward&&p.pending_unique==event,"result enters unique award");return p;
    };
    unsigned rims=0,others=0;
    for(const auto& event:catalog.events().records){
        const auto* media=catalog.media(event.number);if(!media||media->unique_categories.empty())continue;
        const auto p=earn(event.number);CareerGarage m;m.reset_unique(catalog,p);
        const bool rim=std::find(media->unique_categories.begin(),media->unique_categories.end(),3U)!=media->unique_categories.end();
        if(rim){check(event.number==47&&media->sender=="Samantha"&&m.slot()==17,"Samantha rim reward identity");++rims;}
        else{check(m.road_target()==20,"other awards retain generic motion");++others;}
    }
    check(rims==1&&others>0,"campaign award coverage");
    auto p=earn(47);CareerGarage menu;menu.reset_unique(catalog,p);bool purchased=false;
    check(menu.page()==GaragePage::unique_parts&&menu.focus_slot()==17&&menu.entries().size()>1,"actual reward rim selector");
    std::cout<<"REWARD_RIMS event=47 sender=Samantha target_kmh="<<menu.road_target()<<" choices="<<menu.entries().size()<<'\n';
    const auto original_ride=p.ride.installed;const auto bank=p.bank;const auto initial=menu.selected();menu.move(1);
    check(menu.unique_intro()&&menu.selected()==initial,"explanation gates navigation");menu.back(catalog,p);
    check(p.pending_unique==47&&menu.unique_intro(),"back cannot discard award");
    check(menu.accept(catalog,p,purchased,error)&&!purchased&&!menu.unique_intro(),error);
    CareerGarage resumed;resumed.reset_unique(catalog,p);
    check(resumed.focus_slot()==17&&resumed.road_target()==menu.road_target(),"resumed award restores inspection mode");
    std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;
    check(load_race_image(root/"Global/GLOBALB.BUN",globals,issues),"read-only reward globals");
    CareerRideSetup installed;check(resolve_career_ride(globals,catalog,p,installed,error),error);
    FrontendGpuScene visible;check(visible.load_garage(root,1280,720,{installed},error),error);
    std::string signature;std::vector<unsigned> indices;std::vector<CareerRideSetup> variants;
    const auto apply=[&]{
        visible.focus_part(menu.focus_slot());visible.road_target(menu.road_target());
        const auto next=menu.preview_signature(p);
        if(next!=signature){
            check(menu.prepare_previews(globals,catalog,p,variants,indices,error),error);
            FrontendGpuScene prepared;std::string job_error;const auto room=visible.environment();const auto source=visible.car_source();
            auto ready=std::async(std::launch::async,[&]{return prepared.load_garage(root,1280,720,variants,job_error,nullptr,room,source);});
            while(ready.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){
                check(visible.frame(0,1000,error),error);std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(ready.get(),job_error);check(visible.adopt_garage(prepared,error,menu.previewing()?indices.at(menu.preview_index()):0),error);
            check(visible.environment()==room&&prepared.environment()==room,"publication retains shared garage");
            check(validate_recorded_commands(prepared.commands(),error)&&validate_recorded_commands(visible.commands(),error),error);signature=next;
        }
        check(visible.select_part(menu.previewing()?indices.at(menu.preview_index()):0,error)&&visible.frame(0,1000,error),error);
        check(!visible.car_transitioning(),"rim selection does not run car travel");
    };apply();
    CarAssets assets;check(load_car_assets(root/"Global/GLOBALB.BUN",p.car,assets,error),error);
    StockFrameInputs inputs;check(read_car_frame_inputs(root,assets,inputs,true,error),error);
    StockFrame model;const auto& setup=variants.at(indices.at(menu.preview_index()));
    check(prepare_stock_frame(inputs,setup.request,setup.skin,model,error,true),error);
    std::array<std::size_t,4> wheels;wheels.fill(kNoHandle);std::size_t body=kNoHandle;
    const auto& commands=visible.commands().owning_draws();
    for(std::size_t i=0;i<commands.size();++i){
        const auto& d=commands[i];if(d.frontend_sprite_view||d.name.find("car group ")==std::string::npos||d.name.find("_FE_")!=std::string::npos)continue;
        const auto& instance=model.scene.instances.at(d.instance);
        if(instance.part_slot==17&&instance.copy<4)wheels[instance.copy]=i;
        else if(body==kNoHandle&&instance.part_slot!=16)body=i;
    }
    check(body!=kNoHandle&&std::none_of(wheels.begin(),wheels.end(),[](auto i){return i==kNoHandle;}),"independent model identifies all four submitted wheels");
    const auto measure=[&](unsigned frames){
        std::array<double,4> angle{};DrawInstanceUniform body_before=visible.commands().owning_draws()[body].placement;
        for(unsigned frame=0;frame<frames;++frame){
            std::array<DrawInstanceUniform,4> before;for(unsigned i=0;i<4;++i)before[i]=visible.commands().owning_draws()[wheels[i]].placement;
            check(visible.frame(1.F/60,1000+frame*4000/60,error),error);const auto& draws=visible.commands().owning_draws();
            for(unsigned i=0;i<4;++i){
                const auto& a=before[i].rows;const auto& b=draws[wheels[i]].placement.rows;
                unsigned row=0;while(row<3&&a[row*4]*a[row*4]+a[row*4+2]*a[row*4+2]<.1F)++row;check(row<3,"wheel radial basis");
                angle[i]+=std::abs(std::remainder(std::atan2(b[row*4+2],b[row*4])-std::atan2(a[row*4+2],a[row*4]),6.283185307179586));
            }
            check(std::equal(body_before.rows,body_before.rows+16,draws[body].placement.rows),"preview body stays stationary");
        }return angle;
    };
    measure(360);const auto slow=measure(60);
    // Negative control: same native integrator and wheel bank, generic target.
    visible.road_target(20);measure(180);const auto generic=measure(60);
    for(unsigned i=0;i<4;++i)std::cout<<"REWARD_RIMS wheel="<<i<<" inspection_rad_s="<<slow[i]<<" generic_rad_s="<<generic[i]<<'\n';
    float expected=0;const auto word=kNativeGarageMotion[1][1];std::memcpy(&expected,&word,sizeof(expected));
    check(menu.road_target()==expected,"Samantha reward reuses ordinary rim inspection preset");
    for(unsigned i=0;i<4;++i)check(slow[i]>.1&&slow[i]<1.5&&generic[i]>slow[i]*10,"four actual wheels slow only for inspection");
    apply();measure(180);
    const auto capture=[&](const char* name){
        if(output.empty())return;
        std::vector<unsigned char> pixels;check(visible.render_reference(pixels,error),error);
        FrontendArt art;check(art.load(root,error,false)&&art.load_career_pages(root,catalog,error),error);
        art.draw_career({pixels.data(),1280,720,1280*4},catalog,p,0,"",&menu);
        std::ofstream file(output/name,std::ios::binary);file<<"P6\n1280 720\n255\n";
        for(std::size_t i=0;i<pixels.size();i+=4)file.write(reinterpret_cast<const char*>(pixels.data()+i),3);
        check(bool(file),"rim capture saved");
    };capture("reward-rims-first.ppm");
    const auto shape=[&]{std::string hashes;
        for(const auto& d:visible.commands().owning_draws())if(d.name.find("car group ")!=std::string::npos){
            const auto& r=visible.commands().resource_table().resources.at(d.vertex_resource);hashes+=sha256_hex(sha256(r.data,std::size_t(r.bytes)));
        }return hashes;
    };
    const auto first_shape=shape();const auto part=menu.entries()[menu.selected()].part;menu.move(1);apply();
    check(menu.entries()[menu.selected()].part!=part&&shape()!=first_shape,"next changes actual rim geometry");
    check(menu.road_target()==1&&p.ride.installed==original_ride&&p.bank==bank,"next stays slow without spending or installing");
    capture("reward-rims-next.ppm");menu.move(-1);apply();check(shape()==first_shape,"previous restores first rim preview");
    check(menu.accept(catalog,p,purchased,error)&&!purchased&&menu.confirming(),error);menu.back(catalog,p);apply();
    check(!menu.confirming()&&p.pending_unique==47&&p.ride.installed==original_ride&&menu.road_target()==1,"back cancels confirmation and retains inspection");
    check(menu.accept(catalog,p,purchased,error)&&menu.confirming(),error);
    check(menu.accept(catalog,p,purchased,error)&&purchased,error);
    check(!p.pending_unique&&p.ride.installed[17]==part&&p.bank==bank,"confirm installs exact free award");
    check(menu.road_target()==20,"confirmed award releases inspection override");
    if(p.page==CareerPage::magazines)check(catalog.dismiss_magazines(p,error),error);
    check(p.page==CareerPage::menu,"award returns to career menu");
    visible.road_target(50);measure(240);const auto outside=measure(60);
    for(unsigned i=0;i<4;++i)check(outside[i]>slow[i]*25,"ordinary frontend speed resumes after award");
    p.page=CareerPage::garage;menu.reset(catalog,p);apply();check(menu.road_target()==20,"return to ordinary garage uses generic motion");
    const auto enter=[&]{check(menu.accept(catalog,p,purchased,error),error);apply();};
    enter();menu.move(1);enter();check(menu.page()==GaragePage::rims&&menu.road_target()==1,"normal rim brand speed");
    enter();check(menu.page()==GaragePage::rim_styles&&menu.road_target()==1,"normal rim style speed");
    enter();check(menu.page()==GaragePage::rim_sizes&&menu.road_target()==1,"normal rim size speed");
    for(unsigned i=0;i<3;++i){check(!menu.back(catalog,p),"normal rim parent navigation");apply();}
    check(menu.page()==GaragePage::visual&&menu.road_target()==20,"leaving normal rims restores generic preset");
    check(!menu.back(catalog,p)&&menu.back(catalog,p),"normal garage can exit");
    menu.reset_unique(catalog,earn(15));check(menu.road_target()==20,"later hood award does not inherit slowdown");
    std::cout<<"PASS reward-rims enter/next/previous/back/confirm/exit, resident publication, four wheels; other awards="<<others<<'\n';
}
}
