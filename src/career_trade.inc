// Original4C3240/5A1630,57FB80 and4C2D20. Included by career.cpp.
bool CareerCatalog::car_available(const CareerProfile& p,const std::string& name)const {
    const auto car=std::find_if(cars_.begin(),cars_.end(),[&](const auto& c){return c.name==name;});
    if(car==cars_.end())return false;
    if(free_drive_)return true;
    const auto rewards=std::find_if(car_unlocks_.begin(),car_unlocks_.end(),[&](const auto& r){return r.first==name;});
    if(rewards==car_unlocks_.end())return false;
    if(rewards->second.empty())return true;
    return std::any_of(rewards->second.begin(),rewards->second.end(),[&](unsigned event){return p.status[event]==kEventStatusWon;});
}
bool CareerCatalog::prepare_trade(const CareerProfile& p,const std::string& name,CareerProfile& out,int& cost,std::string& error)const {
    if(p.page!=CareerPage::garage||p.car.empty()||name==p.car||!car_available(p,name))
        return refuse(error,"selected trade car is unavailable");
    auto next=p;next.car=name;next.ride.installed={};
    const auto& db=tuning_.parts();
    const auto car_index=[&](const std::string& car){return std::size_t(std::find_if(rating_records_.begin(),rating_records_.end(),[&](const auto& r){return r.first==car;})-rating_records_.begin());};
    const auto coupe=[](std::size_t i){return i<2||i==5;};
    for(unsigned slot=0;slot<120;++slot)if(const auto hash=p.ride.installed[slot]) {
        const auto* old=tuning_.find_part(p.car,slot,hash);if(!old)continue;
        const CarPartRecord* part=nullptr;
        if(slot==1||slot==2||slot==6||slot==7||slot==8||slot==9||slot==10||slot==11||slot==13||slot==14) {
            const auto style=slot==11&&coupe(car_index(name))&&!coupe(car_index(p.car))?1:old->byte_12;
            const auto found=std::find_if(db.records.begin(),db.records.end(),[&](const auto& candidate){
                return candidate.slot==int(slot)&&candidate.car_type_hash==bin_hash(name)&&
                    candidate.upgrade_level==old->upgrade_level&&candidate.byte_12==style;
            });
            if(found!=db.records.end())part=&*found;
        }else if(slot==15||slot==17) {
            if(old->upgrade_level)part=tuning_.find_part(name,slot,hash);
        }else if(slot>=19&&slot<=29) {
            // Native decal-layout names contain the car prefix; rebuild that
            // prefix while preserving the authored zone and size selection.
            if(old->name.rfind(p.car+"_",0)==0)
                part=tuning_.find_part(name,slot,bin_hash(name+old->name.substr(p.car.size())));
        }else if(slot>=30&&slot!=35&&slot!=36&&slot!=58&&
            !(slot>=68&&slot<=74)&&!(slot>=76&&slot<=82)&&slot!=89&&slot!=90&&slot!=97&&slot!=98&&
            !(slot>=101&&slot<=106)&&!(slot>=109&&slot<=114)) {
            part=tuning_.find_part(name,slot,hash);
            if(!part&&slot>=31&&slot<=34&&old->byte_12==17&&old->name.rfind(p.car+"_",0)==0)
                part=tuning_.find_part(name,slot,bin_hash(name+old->name.substr(p.car.size())));
        }
        if(!part)continue;
        next.ride.installed[slot]=part->part_name_hash;
        const int category=CareerTuningCatalog::slot_category(part->slot);
        if(category>=0&&part->upgrade_level&&!owns(next,unsigned(category),tuning_.ownership_key(*part))) {
            auto& inventory=next.ride.owned[unsigned(category)];const auto free=std::find(inventory.begin(),inventory.end(),0U);
            if(free==inventory.end())return refuse(error,"trade ownership inventory full");
            *free=tuning_.ownership_key(*part);
        }
    }
    const auto rating=[&](const CareerProfile& profile,float& value) {
        const auto record=std::find_if(rating_records_.begin(),rating_records_.end(),[&](const auto& r){return r.first==profile.car;});
        if(record==rating_records_.end())return false;
        std::array<unsigned char,kVehicleRecordSize> tuned{};std::vector<VehicleTuningIssue> issues;
        if(!apply_vehicle_tuning(record->second.data(),record->second.size(),career_performance_levels(*this,profile),tuned.data(),tuned.size(),issues))return false;
        VehicleHandling handling;std::vector<VehicleHandlingIssue> handling_issues;float slip=0;
        return decode_vehicle_handling(tuned.data(),tuned.size(),handling,handling_issues)&&ai_setup_performance(handling,value,slip);
    };
    float old_rating=0,new_rating=0;
    if(!rating(p,old_rating)||!rating(next,new_rating))return refuse(error,"trade performance rating unavailable");
    // Native6F0D58/6F0D5C are140/771. A positive cost spends money;
    // trading down credits the difference. Sticker prices are not used.
    const float credit=(((old_rating-140.F)*10.F)/631.F-((new_rating-140.F)*10.F)/631.F)*3500.F;
    if(!std::isfinite(credit)||std::abs(credit)>float(std::numeric_limits<int>::max()/2))return refuse(error,"trade credit invalid");
    cost=-int(credit);out=std::move(next);error.clear();return true;
}
bool CareerCatalog::trade(CareerProfile& p,const std::string& name,std::string& error)const {
    if(!valid(p,error))return false;
    CareerProfile next;int cost=0;if(!prepare_trade(p,name,next,cost,error))return false;
    const auto balance=std::int64_t(p.bank)-cost;
    if(balance<0)return refuse(error,"insufficient career funds");
    if(balance>std::numeric_limits<std::uint32_t>::max())return refuse(error,"career bank overflow");
    next.bank=std::uint32_t(balance);if(!valid(next,error))return false;
    p=std::move(next);error.clear();return true;
}
