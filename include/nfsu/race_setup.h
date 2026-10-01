#pragma once
#include "nfsu/vehicle_tuning.h"
#include <filesystem>

namespace nfsu {
// CLI names are table identities, not inferred retail menu labels. Items
// are not exposed by this first opt-in race setup.
bool parse_performance_levels(const std::string& text, VehicleUpgradeLevels& levels,
                              std::string& error);
std::string performance_levels_text(const VehicleUpgradeLevels& levels);
VehicleUpgradeLevels performance1_levels();
bool performance_levels_equal(const VehicleUpgradeLevels& a, const VehicleUpgradeLevels& b);
bool make_race_vehicle_spec(const unsigned char* original, std::size_t size,
                            const VehicleUpgradeLevels& levels, VehicleSpec& spec,
                            std::string& error);
bool load_race_vehicle_spec(const std::filesystem::path& global, const std::string& car,
                            const VehicleUpgradeLevels& levels, VehicleSpec& spec,
                            std::string& error);
} // namespace nfsu
