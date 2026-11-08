#include "nfsu/career_ride.h"
#include "nfsu/frontend_preset.h"
#include "nfsu/texture.h"
#include <algorithm>
namespace nfsu {
VehicleUpgradeLevels career_performance_levels(const CareerCatalog& catalog,const CareerProfile& profile) {
    VehicleUpgradeLevels levels;
    const auto* bonus=catalog.bonus_car(profile.bonus_preset);
    const auto& v=bonus?bonus->performance:profile.ride.performance;
    levels.weight=int(v[0]);levels.suspension=int(v[1]);levels.engine_a=int(v[2]);
    levels.induction=int(v[3]);levels.nitrous=int(v[4]);levels.engine_b=int(v[5]);
    levels.gearbox=int(v[6]);levels.tyres=int(v[7]);levels.grip=int(v[8]);
    const auto installed_level=[&](unsigned slot) {
        if(bonus) {
            const auto index=bonus->parts.records[slot];
            if(!index||*index==kNoCarPart||*index>=catalog.tuning().parts().records.size())return 0;
            const int level=catalog.tuning().parts().records[*index].upgrade_level;
            return level>0&&level<4?level:0;
        }
        const auto* part=catalog.tuning().find_part(profile.car,slot,profile.ride.installed[slot]);
        const int level=part?int(part->upgrade_level):0;
        // Original575B30 applies an aero table only for levels1..3. A unique
        // visual level4 is not a fourth physics table (nor an Extreme upgrade).
        return level>0&&level<4?level:0;
    };
    const std::array<bool,9> no_unique{};
    const auto& items=bonus?no_unique:profile.ride.unique;
    levels.mass_item=items[0];levels.torque_item_a=items[2];
    levels.torque_item_c=items[3];levels.nitrous_item=items[4];
    levels.torque_item_b=items[5];levels.drive_split_item=items[6];
    levels.tyre_friction_item=items[7];levels.grip_item=items[8];
    levels.aero_a=installed_level(11);levels.aero_b=installed_level(10);
    return levels;
}

bool resolve_career_ride(const std::vector<unsigned char>& globals,
    const CareerCatalog& catalog,const CareerProfile& profile,CareerRideSetup& out,std::string& error) {
    const auto& db=catalog.tuning().parts();
    const auto types=parse_car_type_table(globals.data(),globals.size());
    const auto type=std::find_if(types.records.begin(),types.records.end(),[&](const auto& t){return t.name==profile.car;});
    if(!db.ok()||!types.ok()||type==types.records.end()){error="career ride type unavailable";return false;}
    StockParts stock;
    if(!select_stock_parts(db,profile.car,stock,error))return false;
    CareerRideSetup next;next.request=stock_car_request(profile.car);
    const auto* bonus=catalog.bonus_car(profile.bonus_preset);
    if(!profile.bonus_preset.empty()&&(!catalog.free_drive()||!bonus||bonus->car!=profile.car)) {
        error="invalid Quick Race bonus identity";return false;
    }
    for(unsigned slot=0;slot<120;++slot)next.request.installed_parts.records[slot]=stock.slots[slot].record;
    for(unsigned slot=0;!bonus&&slot<120;++slot)if(const auto hash=profile.ride.installed[slot]) {
        const auto* record=catalog.tuning().find_part(profile.car,slot,hash);
        if(!record){error="career installed part unavailable";return false;}
        next.request.installed_parts.records[slot]=record->index;
    }
    // Native57FB80 case16 chooses the shared brake model by performance[8].
    // A performance package must replace the stock calliper before its paint
    // material can be bound by56B...; changing only physics leaves it black.
    if(bonus)next.request.installed_parts=bonus->parts;
    else {
        const auto brake=find_car_part(db,bin_hash("BRAKES"),16,kAnyPartName,kNoCarPart,int(profile.ride.performance[8]));
        if(brake==kNoCarPart){error="career brake performance model unavailable";return false;}
        next.request.installed_parts.records[16]=brake;
    }
    // Default render paint is supplied by CarTypeInfo+C5C, not a purchased
    // RideInfo paint pointer. It must not award visual reputation.
    next.reputation=catalog.tuning().reputation(next.request.installed_parts);
    auto& base=next.request.installed_parts.records[30];
    if(!base||*base==kNoCarPart) {
        const auto record=find_car_part(db,kAnyCarType,30,type->base_paint_part_hash,kNoCarPart,kAnyUpgradeLevel);
        if(record==kNoCarPart){error="career original base paint unavailable";return false;}
        base=record;
    }
    next.skin=stock_car_skin(profile.car);
    if(!resolve_frontend_preset_skin(globals,profile.car,db,next.request.installed_parts,next.skin.selection,error))return false;
    next.performance=career_performance_levels(catalog,profile);
    out=std::move(next);error.clear();return true;
}
}
