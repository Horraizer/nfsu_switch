#pragma once
#include "nfsu/vehicle_tuning.h"
#include <filesystem>

namespace nfsu {
//4B5F79 event+148 ->78A40C;422523/6B7C20 -> driver+1FC.
// Independent of the selected difficulty and of the event's drift AI scalar.
bool race_rival_throttle_cap(unsigned selector,float& cap) noexcept;
// CLI names are table identities, not inferred retail menu labels. Items
// are not exposed by this first opt-in race setup.
bool parse_performance_levels(const std::string& text, VehicleUpgradeLevels& levels,
                              std::string& error);
std::string performance_levels_text(const VehicleUpgradeLevels& levels);
VehicleUpgradeLevels performance1_levels();
bool performance_levels_equal(const VehicleUpgradeLevels& a, const VehicleUpgradeLevels& b);
// Normal runtime uses the complete57F2B0 continuous aero producer, which
// ignores the two visual aero levels. Raw part-table application remains in
// apply_vehicle_tuning. Drift retains its separate setup transformation.
bool make_race_vehicle_spec(const unsigned char* original, std::size_t size,
                            const VehicleUpgradeLevels& levels, VehicleSpec& spec,
                            std::string& error,bool drift=false,unsigned difficulty=0);
bool load_race_vehicle_spec(const std::filesystem::path& global, const std::string& car,
                            const VehicleUpgradeLevels& levels, VehicleSpec& spec,
                            std::string& error,bool drift=false,unsigned difficulty=0);
// Original57F700 adaptive performance levels, preserving the selected car.
// Returns its matching physical setup; reference is the actual player's setup.
bool adaptive_rival_setup(const unsigned char*,std::size_t,const VehicleSpec& reference,
    const VehicleUpgradeLevels& reference_levels,VehicleUpgradeLevels& rival_levels,
    VehicleSpec& rival,std::string& error,int fixed_selector=-1);
unsigned race_performance_category(float rating) noexcept;
} // namespace nfsu
