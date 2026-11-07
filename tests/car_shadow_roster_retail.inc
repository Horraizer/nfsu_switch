// Production shadow packages for every playable stock/dressed car and UG0.
namespace {
void verify_shadow_roster(const std::filesystem::path& root) {
    const auto check=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};
    std::string error;CareerCatalog catalog;check(catalog.load_free_drive(root,error),error);
    std::vector<unsigned char> global;std::vector<RaceTableIssue> issues;
    check(load_race_image(root/"Global/GLOBALB.BUN",global,issues),"shadow GLOBALB input");
    const auto flat=[](const VehicleVec3& point,TrackContact& hit,const void*) {
        hit={};hit.point[0]=float(point.x);hit.point[1]=float(point.y);hit.normal[2]=1;return true;
    };
    unsigned checked=0;
    for(const auto& car:catalog.starters())for(unsigned tuned=0;tuned<2;++tuned) {
        CareerProfile p;check(catalog.free_profile(car.name,p,error),error);p.page=CareerPage::garage;
        if(tuned)for(unsigned slot:{1U,2U,6U,7U,8U,10U,11U,13U,14U,15U,17U}) {
            const CarPartRecord* best=nullptr;
            for(const auto& part:catalog.tuning().parts().records)
                if(part.upgrade_level&&catalog.tuning().find_part(p.car,slot,part.part_name_hash)==&part&&
                   (slot!=17||catalog.tuning().wheel_fits(p.car,part))&&(!best||part.upgrade_level>best->upgrade_level))best=&part;
            if(best)check(catalog.buy_part(p,slot,best->part_name_hash,error),error);
        }
        CareerRideSetup dress;check(resolve_career_ride(global,catalog,p,dress,error),error);
        CarAssets assets;check(load_car_assets(root/"Global/GLOBALB.BUN",car.name,assets,error),error);
        StockFrameInputs input;check(read_car_frame_inputs(root,assets,input,true,error),error);
        check(read_exhaust_family_inputs(root,input),"shadow exhaust family input");
        StockFrame frame;check(prepare_race_car_frame(input,root,dress.request,dress.skin,frame,error,false),error);
        CarShadowAsset shadow;check(prepare_car_shadow(car.name,input,frame.car_pack,shadow,error),error);
        check(prepare_car_shadow_bounds(input,frame,shadow,error),error);
        check(shadow.selected_bounds&&shadow.texture.hash==bin_hash(car.name+"_SHADOWIG"),"selected car owns its shadow texture");
        for(unsigned axis=0;axis<3;++axis)
            check(std::isfinite(shadow.low[axis])&&std::isfinite(shadow.high[axis])&&shadow.low[axis]<shadow.high[axis],"selected solid footprint is finite");
        VehicleState state;state.orientation={1,0,0,0};state.position={0,0,.5};VehicleSpec spec;
        const auto projected=project_car_shadow(shadow,state,spec,flat,nullptr);
        check(projected.projected&&projected.alpha>0&&projected.strip_mask==7,"pre-wheel-contact shadow remains visible");
        RecordedCommands commands;RenderOptions options;
        check(record_car_shadow_commands(shadow,projected,options,commands,error),error);
        check(commands.owning_uploads().resources.at(2).texture.mip_count==shadow.texture.mip_count,"shadow mips survive upload");
        const auto& draw=commands.owning_draws().at(0);
        check(draw.program&&std::string(draw.program->name)=="car_shadow_textured","all roster shadows use projected shader");
        for(unsigned i=0;i<4;++i)check(draw.material.environment_color[i]==projected.road_heights[12+i],"last GPU height row survives packet");
        std::cout<<"SHADOW_ROSTER "<<car.name<<" tuned="<<tuned<<" bounds";
        for(auto x:shadow.low)std::cout<<' '<<x;for(auto x:shadow.high)std::cout<<' '<<x;
        std::cout<<" texture="<<shadow.texture.hash<<'\n';++checked;
    }
    IntroGrid grid;check(load_intro_grid(root,grid,error),error);
    for(const auto& actor:grid.actors) {
        const auto p=place_car_shadow(actor.shadow,actor.state,actor.placement_spec,grid.course.world);
        check(p.projected&&p.alpha>0&&p.strip_mask,"UG0 authored tuned car has an initial projected shadow");
        std::cout<<"SHADOW_UG0 "<<actor.assets.car_type_name<<" alpha="<<p.alpha<<" mask="<<p.strip_mask<<'\n';
    }
    for(const auto& actor:grid.traffic)if(actor.shadow.texture.usable()) {
        const auto p=place_car_shadow(actor.shadow,actor.state,actor.placement_spec,grid.course.world);
        check(p.projected&&p.alpha>0&&p.strip_mask,"UG0 traffic has its authored projected shadow");
    }
    std::cout<<"PASS shadow roster "<<checked<<" packages plus original UG0 racer/traffic grid\n";
}
}
