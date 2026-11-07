// Original57F700; settings use automatic nitrous copying and tolerance5.
unsigned race_performance_category(float rating) noexcept {
    //57F691..57F6BB, thresholds6B5BD8: strict comparisons.
    return rating>500.F?0U:rating>390.F?1U:rating>280.F?2U:3U;
}
bool adaptive_rival_setup(const unsigned char* original,std::size_t size,const VehicleSpec& reference,
    const VehicleUpgradeLevels& reference_levels,VehicleUpgradeLevels& rival_levels,
    VehicleSpec& rival,std::string& error,int fixed_selector) {
    float wanted=0,unused=0;
    if(!ai_setup_performance(reference.handling,wanted,unused)){error="adaptive reference rating refused";return false;}
    if(fixed_selector< -1||fixed_selector>10){error="fixed rival rating selector refused";return false;}
    //4B421E selects57F970 for authored fixed selectors.6F0D58/5C are
    //140/771;57F970 keeps its current levels between tolerance expansions.
    if(fixed_selector>=0)wanted=float((771.F-140.F)*float(fixed_selector)*.1+140.F);
    auto levels=rival_levels;VehicleSpec candidate;float rating=0;
    const auto evaluate=[&]() {
        //57F2B0 clears RideInfo+2C/+30: the two aero levels (not item words).
        //57F579 adds continuous aero from weight/suspension/tyre levels,
        //then publishes its truncated level for both aero metadata fields.
        levels.aero_a=levels.aero_b=0;
        if(!make_race_vehicle_spec(original,size,levels,candidate,error))return false;
        const float normalized=float(levels.weight+levels.suspension+levels.tyres)*.1111111119389534F;
        levels.aero_a=levels.aero_b=static_cast<int>(normalized*3.F);
        return ai_setup_performance(candidate.handling,rating,unused);
    };
    if(!evaluate())return false;
    float tolerance=5.F;
    constexpr unsigned masks[15]={1,2,4,8,3,5,6,9,10,12,7,11,13,14,15}; //6F0F78
    int VehicleUpgradeLevels::*driving[4]={&VehicleUpgradeLevels::engine_a,&VehicleUpgradeLevels::engine_b,
        &VehicleUpgradeLevels::weight,&VehicleUpgradeLevels::induction}; //6F0F58
    int VehicleUpgradeLevels::*secondary[4]={&VehicleUpgradeLevels::suspension,&VehicleUpgradeLevels::gearbox,
        &VehicleUpgradeLevels::tyres,&VehicleUpgradeLevels::grip}; //6F0F68
    while(std::abs(wanted-rating)>tolerance) {
        if(fixed_selector<0){levels=reference_levels;if(!evaluate())return false;} //22 copied words; selected car retained.
        for(unsigned mask:masks) {
            if(std::abs(wanted-rating)<=tolerance)break;
            for(unsigned i=0;i<4 && std::abs(wanted-rating)>tolerance;++i)if(mask&(1U<<i)) {
                levels.*driving[i]=std::clamp(levels.*driving[i]+(rating<wanted?1:-1),0,3);
                if(!evaluate())return false;
            }
            //57F87A..57F8F6 updates all secondary levels even if the last
            //driving adjustment entered tolerance; then re-evaluates rating.
            float average=0;
            for(auto member:driving)average+=float(levels.*member)*.25F;
            const int level=std::clamp(static_cast<int>(average+.5F),0,3);
            for(auto member:secondary)levels.*member=level;
            levels.nitrous=reference_levels.nitrous;
            if(!evaluate())return false;
        }
        if(std::abs(wanted-rating)>tolerance) {
            tolerance+=tolerance;
            if(!std::isfinite(tolerance)){error="adaptive tolerance overflow";return false;}
        }
    }
    rival_levels=levels;rival=candidate;return true;
}
