#pragma once
#include "nfsu/car_parts.h"
#include "nfsu/race_events.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace nfsu {
struct CareerTuningPrice {
    bool present=false;
    std::array<std::uint32_t,3> purchase{};
    std::array<std::uint32_t,4> visual{};
};
struct CareerStyleUnlock {
    std::uint32_t points=0,key=0,message=0;
    unsigned kind=0,group=0,level=0;
};
// Original catalogue, not a shop UI or an installed/purchased inventory.
// Point thresholds consume the earned high-water mark: native4AA970 never
// clears an earned flag when a later penalty lowers the current point bank.
class CareerTuningCatalog {
public:
    bool load(const std::vector<unsigned char>& front,
              const std::vector<unsigned char>& global,
              const RaceEventTable&,std::string& error);
    bool ready()const{return ready_;}
    const CarPartDatabase& parts()const{return parts_;}
    const std::vector<CareerStyleUnlock>& style_rewards()const{return style_;}
    static int slot_category(int slot)noexcept;
    static int performance_category(unsigned field)noexcept;
    std::uint32_t price(unsigned category,unsigned level)const noexcept;
    std::uint32_t part_price(const CarPartRecord&)const noexcept;
    std::uint32_t ownership_key(const CarPartRecord&)const noexcept;
    const CarPartRecord* find_part(const std::string& car,unsigned target_slot,
                                   std::uint32_t part_hash)const noexcept;
    // The style list uses the tier's default rim diameter. The size list
    // accepts every rim diameter with the car's required tyre diameter.
    // FrontB 30240, indexed by the original internal car name.
    const std::array<unsigned char,16>* decal_fitment(std::uint32_t car)const noexcept;
    bool wheel_fits(const std::string& car,const CarPartRecord& part,bool default_rim=true)const noexcept;
    bool part_available(const CarPartRecord&,const std::array<std::uint8_t,256>& status,
                        std::uint32_t earned_high_water)const;
    std::uint32_t visual_points(unsigned category,unsigned level)const noexcept;
    float reputation(const InstalledPartSelection&)const noexcept;
    int required_event(unsigned category,unsigned level)const noexcept;
    bool category_available(unsigned category,unsigned level,
                            const std::array<std::uint8_t,256>& status)const noexcept;
    bool style_available(unsigned kind,std::uint32_t key,
                         std::uint32_t earned_high_water)const noexcept;
private:
    bool ready_=false;
    CarPartDatabase parts_;
    std::vector<std::pair<std::uint32_t,int>> wheel_sizes_;
    std::vector<std::pair<std::uint32_t,std::array<unsigned char,16>>> decal_fitment_;
    std::array<CareerTuningPrice,32> prices_{};
    std::array<std::array<int,5>,32> events_{};
    std::vector<CareerStyleUnlock> style_;
};
}
