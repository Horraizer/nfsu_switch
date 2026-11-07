#pragma once
#include "nfsu/career.h"
#include "nfsu/stock_frame.h"
#include "nfsu/vehicle_tuning.h"
namespace nfsu {
// Indices refer to the authenticated GLOBALB database. The same selection is
// used by garage previews, race assembly, shadow bounds and visual reputation.
VehicleUpgradeLevels career_performance_levels(const CareerCatalog&,const CareerProfile&);
struct CareerRideSetup {
    StockSceneRequest request;
    StockFrameSkin skin;
    VehicleUpgradeLevels performance;
    float reputation=0;
    unsigned garage_paint_slot=0;
};
bool resolve_career_ride(const std::vector<unsigned char>& globals,
    const CareerCatalog&,const CareerProfile&,CareerRideSetup&,std::string& error);
}
