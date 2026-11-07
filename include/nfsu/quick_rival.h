#pragma once
#include "nfsu/car_parts.h"
#include "nfsu/vehicle_tuning.h"
namespace nfsu {
// PC1.4 57D860: random appearance after 57F700 performance matching.
// Uses the caller's native RNG state; no runtime RNG is consumed while drawing.
bool quick_rival_visual(const CarPartDatabase& db,const std::string& car,
    const unsigned char* record,std::size_t size,const VehicleUpgradeLevels& levels,
    std::uint32_t& rng,InstalledPartSelection& out,std::string& error);
std::uint32_t quick_rival_random(std::uint32_t& state,unsigned bound) noexcept;
// 4B4590 inclusive cumulative comparison, independent of vehicle upgrades.
int quick_rival_context(const unsigned char* ai,unsigned difficulty,std::uint32_t& rng) noexcept;
}
