#pragma once
#include "nfsu/car_parts.h"
#include <utility>
namespace nfsu {
inline constexpr std::uint32_t kKrVinylPart=0xcbb764e7U;
// Owner-provided cosmetic extension. Retail parsing and authored records stay unchanged.
inline void append_kr_vinyl_part(CarPartDatabase& db) {
    if(!db.ok())return;
    for(const auto& p:db.records)if(p.part_name_hash==kKrVinylPart)return;
    CarPartRecord p;p.index=db.records.size();p.name="KR";p.car_type_hash=0xffffffffU;
    p.part_name_hash=kKrVinylPart;p.slot=31;p.upgrade_level=1;p.byte_12=15;
    db.records.push_back(std::move(p));
}
struct PreparedCarScene;
bool apply_kr_vinyl(const PreparedCarScene&,const std::string& car,unsigned width,unsigned height,
                    std::vector<unsigned char>& rgba,std::string& error);
} // namespace nfsu
