// Same menu, preview, catalog and save adapter as the normal garage.
namespace {
#include "performance_packages_native.inc"
void verify_performance_packages(const std::filesystem::path& root,const nfsu::CareerCatalog& catalog,
                                 const std::filesystem::path& capture={}) {
    using namespace nfsu;
    const auto check=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};
    CareerProfile initial;std::string error;bool purchased=false;
    check(catalog.intro_won(initial,error)&&catalog.movie_finished(initial,error),error);
    const auto civic=std::find_if(catalog.starters().begin(),catalog.starters().end(),[](const auto& car){return car.name=="CIVIC";});
    check(civic!=catalog.starters().end(),"package starter present");
    check(catalog.buy(initial,std::size_t(civic-catalog.starters().begin()),error)&&catalog.movie_finished(initial,error),error);
    initial.page=CareerPage::garage;initial.customization_seen=true;initial.bank=1000000;const auto early=initial;
    for(unsigned id=0;id<initial.status.size();++id)if(find_race_event(catalog.events(),id))initial.status[id]=kEventStatusWon;
    initial.style_points=initial.style_high_water=1000000;const auto& tuning=catalog.tuning();
    check(tuning.performance_packages().size()==std::size(kNativePerformancePackages),"native package extent");
    for(const auto& row:kNativePerformanceGroups)for(unsigned level=1;level<=3;++level)
        check(std::count_if(tuning.performance_packages().begin(),tuning.performance_packages().end(),
            [&](const auto& p){return p.field==row[0]&&p.level==level;})==row[level],"native tier variant count");
    std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;
    check(load_race_image(root/"Global/GLOBALB.BUN",globals,issues),"read-only package global input");
    FrontendArt art;check(art.load(root,error,false)&&art.load_career_pages(root,catalog,error),error);
    const auto enter=[&](CareerGarage& menu,CareerProfile& p){check(menu.accept(catalog,p,purchased,error),error);};
    const auto tier=[&](CareerGarage& menu,CareerProfile& p,unsigned field,unsigned level) {
        menu.reset(catalog,p);menu.move(1);enter(menu,p);check(menu.page()==GaragePage::performance,"normal performance entry");
        const auto item=std::find_if(menu.entries().begin(),menu.entries().end(),[&](const auto& e){return e.value==field;});
        check(item!=menu.entries().end(),"normal category");menu.move(int(item-menu.entries().begin()));enter(menu,p);
        check(menu.page()==GaragePage::packages,"normal level entry");menu.move(int(level)-int(menu.selected()));
    };
    const auto stable=[](const CareerProfile& a,const CareerProfile& b) {
        return a.bank==b.bank&&a.ride.performance==b.ride.performance&&a.ride.unique==b.ride.unique&&
            a.ride.installed==b.ride.installed&&a.ride.owned==b.ride.owned&&a.ride.turbo_brand==b.ride.turbo_brand&&a.performance_sponsors==b.performance_sponsors;
    };
    if(!capture.empty()){check(!std::filesystem::exists(capture),"fresh performance capture required");std::filesystem::create_directories(capture);}
    unsigned cases=0;
    for(const auto& row:kNativePerformancePackages) {
        const auto field=row[0],level=row[1],number=row[2];const auto* package=tuning.performance_package(field,level,number);
        check(package&&package->brands.size()==row[3],"native package lookup");
        for(unsigned b=0;b<row[3];++b)check(package->brands[b]==row[4+b],"native ordered firms");
        auto p=initial;for(unsigned previous=1;previous<level;++previous)check(catalog.buy_performance(p,field,previous,error),error);
        const auto before=p;CareerGarage menu;tier(menu,p,field,level);enter(menu,p);
        check(!menu.confirming()&&menu.page()==GaragePage::performance_brands,
            "missing performance brand selection: tier opens purchase confirmation directly");
        check(menu.selected()==0&&menu.fng()==0xb82d8c,"original brand page opens first package");
        menu.move(-100);check(menu.selected()==0,"first brand boundary");menu.move(100);check(menu.selected()+1==menu.entries().size(),"last brand boundary");
        menu.move(int(number-1)-int(menu.selected()));const auto& entry=menu.entries()[menu.selected()];std::string detail;
        for(auto brand:package->brands){if(!detail.empty())detail+='^';detail+=tuning.sponsor_name(brand);}
        check(entry.value==number&&entry.upgrade_level==level&&entry.detail==detail&&entry.available&&!entry.installed,"selected package firms and tier");
        std::array<float,3> ratings{},other_ratings{};check(catalog.performance_ratings(menu.preview(p),ratings),"preview ratings");
        for(unsigned i=0;i<menu.entries().size();++i) {
            const auto preview=menu.preview(p,i);
            check(preview.ride.performance[field]==level&&catalog.performance_ratings(preview,other_ratings)&&ratings==other_ratings,"same-level firms never change power");
        }
        check(stable(p,before),"hover leaves durable ride, sponsors and bank unchanged");
        if(number==1) {
            FrontendUiFrame ui;art.record_career(1280,720,catalog,p,0,"",ui,&menu);
            check(!ui.vertices.empty()&&ui.additive.size()<=kFrontendUiMaxQuads,"actual brand page GPU bounds");
            std::vector<CareerRideSetup> variants;std::vector<unsigned> indices;check(menu.prepare_previews(globals,catalog,p,variants,indices,error),error);
            if(field==1||field==8)check(indices.size()==menu.entries().size()&&indices[0]>0,"normal mechanical preview bank");
            if(!capture.empty()&&field==2&&level==1) {
                FrontendGpuScene scene;check(scene.load_garage(root,1280,720,variants,error)&&scene.frame(0,1000,error),error);
                std::vector<unsigned char> rgba;check(scene.render_reference(rgba,error),error);art.draw_career({rgba.data(),1280,720,1280*4},catalog,p,0,"",&menu);
                std::ofstream image(capture/"engine-street-brands.tga",std::ios::binary);check(write_tga(image,1280,720,rgba,error),error);
            }
        }
        enter(menu,p);check(menu.confirming()&&!purchased&&stable(p,before),"confirmation leaves purchase untouched");
        check(!menu.back(catalog,p)&&!menu.confirming()&&stable(p,before),"B cancels transaction");
        enter(menu,p);menu.move(1);enter(menu,p);check(!menu.confirming()&&!purchased&&stable(p,before),"Cancel button cancels transaction");
        enter(menu,p);enter(menu,p);const auto category=unsigned(CareerTuningCatalog::performance_category(field));
        check(purchased&&menu.page()==GaragePage::packages&&p.bank==before.bank-tuning.price(category,level)&&p.ride.performance[field]==level&&catalog.owns(p,category,level),"normal purchase returns with exact tier price");
        auto expected_sponsors=before.performance_sponsors;for(auto brand:package->brands)expected_sponsors[brand]=true;
        check(p.performance_sponsors==expected_sponsors,"only purchased firms unlock sponsors");
        check(p.ride.installed==before.ride.installed&&p.ride.unique==before.ride.unique,"no unrelated part or unique mutation");
        const auto native=std::find_if(std::begin(kNativePerformanceTurbo),std::end(kNativePerformanceTurbo),[&](const auto& r){return unsigned(r[0])==field&&unsigned(r[1])==level&&unsigned(r[2])==number;});
        check(native!=std::end(kNativePerformanceTurbo)&&p.ride.turbo_brand==(field==3?(*native)[3]:before.ride.turbo_brand),"native turbo identity");
        CareerRideSetup installed;check(resolve_career_ride(globals,catalog,p,installed,error),error);check(installed.turbo_brand==p.ride.turbo_brand,"race assembly retains turbo identity");
        if(field==8)check(tuning.parts().records[*installed.request.installed_parts.records[16]].upgrade_level==level,"actual brake tier model");
        enter(menu,p);check(menu.dialog()==GarageDialog::already_installed,"installed level information dialog");check(!menu.back(catalog,p),"dismiss dialog");
        menu.move(-int(menu.selected()));enter(menu,p);enter(menu,p);check(p.ride.performance[field]==0,"stock removes tier");
        const auto paid=p;menu.move(int(level));enter(menu,p);check(menu.page()==GaragePage::performance_brands&&menu.selected()==0,"owned tier reopens first package");
        menu.move(100);const auto alternate=menu.entries()[menu.selected()].value;check(menu.entries()[menu.selected()].owned&&menu.entries()[menu.selected()].price==0,"owned tier covers every package");
        enter(menu,p);enter(menu,p);check(p.bank==paid.bank&&p.performance_sponsors==paid.performance_sponsors,"reinstall cannot grant another package's sponsors");
        if(field==3)check(p.ride.turbo_brand==CareerTuningCatalog::turbo_brand(*tuning.performance_package(field,level,alternate)),"owned turbo changes sound identity");
        ++cases;
    }
    {
        auto p=early;CareerGarage menu;tier(menu,p,2,1);check(!menu.accept(catalog,p,purchased,error)&&stable(p,early),"locked level cannot spend cash");
        p=initial;p.bank=tuning.price(18,1)-1;const auto poor=p;tier(menu,p,2,1);check(!menu.accept(catalog,p,purchased,error)&&stable(p,poor),"funds checked before brands");
        for(const auto& row:kNativePerformanceProgression) {
            p=initial;for(unsigned level=1;level<=row[0];++level)check(catalog.buy_performance(p,2,level,error),error);
            if(row[1]<=row[0])continue;const auto before=p;
            check(catalog.buy_performance(p,2,row[1],error)==bool(row[2]),"native new-tier progression");if(!row[2])check(stable(p,before),"skipped tier refusal is atomic");
        }
        p=initial;const auto before=p;check(!catalog.buy_performance_package(p,2,1,99,error)&&stable(p,before),"unknown variant cannot spend cash");
    }
    {
        auto p=initial;p.style_points=p.style_high_water=0;CareerGarage menu;
        const auto window=[&] {
            menu.reset(catalog,p);enter(menu,p);menu.move(4);enter(menu,p);enter(menu,p);
            check(menu.page()==GaragePage::decals_brands,"normal windshield brand page");
        };
        const auto selected=[&] {return std::find_if(menu.entries().begin(),menu.entries().end(),[](const auto& e){return e.value==bin_hash("NEUSPEED");});};
        window();check(selected()==menu.entries().end(),"unbought performance sponsor is initially absent");
        check(catalog.buy_performance_package(p,2,1,2,error),error);window();const auto item=selected();
        check(item!=menu.entries().end(),"paid performance firm appears in actual decal browser without style points");
        menu.move(int(item-menu.entries().begin())-int(menu.selected()));const auto part=item->part,price=item->price,bank=p.bank;
        enter(menu,p);enter(menu,p);check(purchased&&p.ride.installed[67]==part&&p.bank==bank-price,"unlocked firm installs through normal decal transaction");
    }
    {
        CareerCatalog quick;check(quick.load_free_drive(root,error),error);CareerProfile p;check(quick.free_profile("CIVIC",p,error),error);p.page=CareerPage::garage;
        p.ride.performance.fill(2);CareerGarage menu;menu.reset(quick,p,true);menu.move(1);
        check(menu.accept(quick,p,purchased,error)&&menu.page()==GaragePage::performance,"fixed lobby exposes brands");menu.move(4);check(menu.entries()[menu.selected()].value==3,"normal turbo category order");
        check(menu.accept(quick,p,purchased,error)&&menu.page()==GaragePage::performance_brands,"fixed lobby opens current tier");
        check(menu.entries().size()==5&&std::all_of(menu.entries().begin(),menu.entries().end(),[](const auto& e){return e.upgrade_level==2&&e.price==0;}),"host tier stays fixed");
        menu.move(3);check(menu.accept(quick,p,purchased,error)&&menu.accept(quick,p,purchased,error)&&purchased,error);
        check(p.ride.turbo_brand==0&&std::all_of(p.ride.performance.begin(),p.ride.performance.end(),[](auto level){return level==2;}),"split ride retains firm without tier edit");
        const auto ride=p.ride;CareerProfile resumed;check(quick.quick_profile("CIVIC",0,resumed,error,&ride),error);check(resumed.ride.turbo_brand==p.ride.turbo_brand,"Quick Ride return retains firm");
        p.ride.performance.fill(0);menu.reset(quick,p,true);check(menu.entries().size()==1,"stock lobby has no brand packages");
    }
    {
        const auto folder=std::filesystem::temp_directory_path()/("nfsu-performance-packages-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(folder);auto p=initial;check(catalog.buy_performance_package(p,3,1,4,error),error);
        check(save_career(folder/"current",catalog,p,error),error);CareerProfile loaded;check(load_career(folder/"current",catalog,loaded,error)&&stable(p,loaded),"save/reload preserves tier, sponsors and turbo");
        std::ifstream input(folder/"current.0",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});input.close();
        auto old=bytes.substr(0,bytes.find("PERFORMANCE_BRANDS "));old.replace(0,std::string("NFSU_CAREER 13").size(),"NFSU_CAREER 12");old+="SHA256 "+sha256_hex(sha256(old.data(),old.size()))+'\n';
        {std::ofstream out(folder/"legacy.0",std::ios::binary);out<<old;check(bool(out),"legacy fixture written");}
        check(load_career(folder/"legacy",catalog,loaded,error)&&loaded.ride.performance==p.ride.performance&&loaded.ride.turbo_brand==-1&&loaded.performance_sponsors==CareerProfile{}.performance_sponsors,"schema12 keeps tiers without invented identity");
        check(save_career(folder/"legacy",catalog,loaded,error),error);std::ifstream backup(folder/"legacy.before-v13.0",std::ios::binary);std::string retained((std::istreambuf_iterator<char>(backup)),{});backup.close();check(retained==old,"schema12 rollback is byte exact");
        loaded.ride.turbo_brand=5;check(!save_career(folder/"legacy",catalog,loaded,error),"invalid identity cannot overwrite save");check(load_career(folder/"legacy",catalog,loaded,error)&&loaded.ride.turbo_brand==-1,"good migrated slot survives rejection");
        std::filesystem::remove_all(folder);
    }
    std::cout<<"PASS normal performance menu: "<<cases<<" native packages, firms/order, preview/cancel/purchase/reinstall, progression, Quick Ride/fixed lobby and schema12/13 persistence\n";
}
}
