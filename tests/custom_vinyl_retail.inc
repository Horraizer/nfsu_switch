// Production KR catalog, skin, save and all-car atlas coverage.
void verify_kr_vinyl(const std::filesystem::path& root,const std::filesystem::path& capture) {
    CareerCatalog catalog;std::string error;const auto check=[&](bool ok,const std::string& detail){if(!ok)throw std::runtime_error(detail+": "+error);};
    check(catalog.load_free_drive(root,error),"KR catalog");
    std::vector<unsigned char> globals;std::vector<RaceTableIssue> issues;check(load_race_image(root/"Global/GLOBALB.BUN",globals,issues),"KR globals");
    const auto temp=std::filesystem::temp_directory_path()/("nfsu-kr-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    if(!capture.empty()){check(!std::filesystem::exists(capture),"fresh KR captures");std::filesystem::create_directories(capture);}
    unsigned count=0;
    for(const auto& car:catalog.cars()) {
        CareerProfile p;check(catalog.free_profile(car.name,p,error),"KR stock profile");p.page=CareerPage::garage;const auto before=p.bank;
        check(catalog.buy_part(p,31,kKrVinylPart,error)&&p.bank==before,"KR free installation");
        check(catalog.valid(p,error),"KR installed profile valid");
        CareerRideSetup ride;check(resolve_career_ride(globals,catalog,p,ride,error)&&ride.skin.selection.kr_vinyl,"KR ride resolution");
        ride.skin.selection.base_color=skin_pack_paint(120,160,200,128);
        CarAssets assets;check(load_car_assets(root/"Global/GLOBALB.BUN",car.name,assets,error),"KR car assets");
        StockFrameInputs inputs;check(read_car_frame_inputs(root,assets,inputs,true,error),"KR skin input");
        auto plain_request=ride.request;plain_request.installed_parts.records[31]=kNoCarPart;auto plain_skin=ride.skin;plain_skin.selection.kr_vinyl=false;
        StockFrame plain,custom;StockFramePaintCache paint_cache;paint_cache.slot=30;check(prepare_stock_frame(inputs,plain_request,plain_skin,plain,error,true),"KR baseline preparation");
        check(prepare_stock_frame(inputs,ride.request,ride.skin,custom,error,true,nullptr,&paint_cache),"KR customized preparation");
        check(plain.skin.rgba.size()==custom.skin.rgba.size()&&!custom.skin.rgba.empty(),"KR real body target");
        unsigned changed=0;for(std::size_t i=0;i<custom.skin.rgba.size();i+=4){
            check(custom.skin.rgba[i+3]==plain.skin.rgba[i+3],"KR preserves paint gloss");
            changed+=custom.skin.rgba[i]!=plain.skin.rgba[i]||custom.skin.rgba[i+1]!=plain.skin.rgba[i+1]||custom.skin.rgba[i+2]!=plain.skin.rgba[i+2];
        }
        check(changed>128,"KR visible atlas coverage");
        auto repaint=ride.skin;repaint.selection.base_color=skin_pack_paint(20,120,80,128);
        std::vector<unsigned char> painted;
        check(prepare_stock_paint_variant(inputs,custom,paint_cache,ride.request.installed_parts,repaint,painted,error)&&painted.size()==custom.skin.rgba.size(),"KR live paint preview");
        unsigned ink=0,retained_ink=0;for(std::size_t i=0;i<painted.size();i+=4)
            if(custom.skin.rgba[i]<8&&custom.skin.rgba[i+1]<8&&custom.skin.rgba[i+2]<8&&
               unsigned(plain.skin.rgba[i])+plain.skin.rgba[i+1]+plain.skin.rgba[i+2]>60) {
                ++ink;retained_ink+=painted[i]<8&&painted[i+1]<8&&painted[i+2]<8;
            }
        if(!(ink>50&&retained_ink*10>=ink*9))std::cerr<<"KR repaint car="<<car.name<<" ink="<<ink<<" retained="<<retained_ink<<'\n';
        check(ink>50&&retained_ink*10>=ink*9,"KR logo retained through repaint preview");
        if(!capture.empty()&&(car.name=="240SX"||car.name=="CIVIC"||car.name=="SKYLINE")) {
            RenderOptions options;options.width=960;options.height=640;options.car_shading=true;options.camera.yaw_deg=45;options.camera.pitch_deg=22;
            check(render_stock_frame(custom,options,error),"KR visible render");std::ofstream file(capture/(car.name+".tga"),std::ios::binary);
            check(write_tga(file,custom.frame.width(),custom.frame.height(),custom.frame.rgba(),error),"KR capture output");
        }
        std::cout<<"PASS KR car="<<car.name<<" changed_texels="<<changed<<'\n';++count;
    }
    check(count==20,"KR all playable cars covered");
    CareerCatalog campaign;check(campaign.load(root,error),"KR career catalog");CareerProfile p;
    check(campaign.intro_won(p,error)&&campaign.movie_finished(p,error),"KR career starter choice");
    const auto civic=std::find_if(campaign.starters().begin(),campaign.starters().end(),[](const auto& car){return car.name=="CIVIC";});
    check(civic!=campaign.starters().end()&&campaign.buy(p,std::size_t(civic-campaign.starters().begin()),error)&&campaign.movie_finished(p,error),"KR career car");
    p.page=CareerPage::garage;p.customization_seen=true;const auto bank=p.bank;
    check(campaign.buy_part(p,31,kKrVinylPart,error)&&p.bank==bank,"KR career gift available without progress unlock");
    check(save_career(temp,campaign,p,error),"KR career save");CareerProfile restored;
    check(load_career(temp,campaign,restored,error)&&restored.ride.installed[31]==kKrVinylPart,"KR career saved selection");
    for(unsigned slot=0;slot<2;++slot){auto file=temp;file+="."+std::to_string(slot);std::filesystem::remove(file);}
}
