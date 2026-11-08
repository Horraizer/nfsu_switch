const CareerBonusCar* CareerCatalog::bonus_car(const std::string& preset)const {
    const auto found=std::find_if(bonus_cars_.begin(),bonus_cars_.end(),
        [&](const auto& car){return car.preset==preset;});
    return found==bonus_cars_.end()?nullptr:&*found;
}
std::vector<std::string> CareerCatalog::quick_cars(std::uint32_t earned)const {
    std::vector<std::string> result;
    if(!free_drive_)return result;
    for(const auto& car:starters_)result.push_back(car.name);
    for(const auto& bonus:bonus_cars_)if(bonus.points<=earned)result.push_back(bonus.preset);
    return result;
}
bool CareerCatalog::quick_profile(const std::string& identity,std::uint32_t earned,
    CareerProfile& profile,std::string& error,const CareerRide* ordinary_ride)const {
    const auto* bonus=bonus_car(identity);
    if(!bonus) {
        CareerProfile next;if(!free_profile(identity,next,error))return false;
        if(ordinary_ride)next.ride=*ordinary_ride;
        profile=std::move(next);return true;
    }
    if(!free_drive_||earned<bonus->points)return refuse(error,"bonus car not yet earned");
    CareerProfile next;if(!free_profile(bonus->car,next,error))return false;
    next.bonus_preset=bonus->preset;
    next.ride.performance=bonus->performance;
    profile=std::move(next);error.clear();return true;
}
std::string CareerCatalog::quick_car_label(const CareerProfile& profile)const {
    const auto* bonus=bonus_car(profile.bonus_preset);
    return bonus?bonus->label:profile.car;
}
