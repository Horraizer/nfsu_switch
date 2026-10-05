#include "nfsu/frontend_preset.h"
#include "nfsu/texture.h"
#include <cstring>
namespace nfsu {
namespace {
std::uint32_t word(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1])<<8) |
        (std::uint32_t(p[2])<<16) | (std::uint32_t(p[3])<<24);
}
std::uint32_t attribute(const CarPartDatabase& db,const CarPartRecord& r,std::uint32_t key) {
    for(int a=0;a<r.attribute_count;++a) {
        const auto& item=db.attributes[r.attribute_index+static_cast<unsigned>(a)];
        if(item.key==key) return item.value;
    }
    return 0;
}
}
bool resolve_frontend_skyline_parts(const std::vector<unsigned char>& front,
    const CarPartDatabase& db,InstalledPartSelection& selection,std::string& error) {
    selection=InstalledPartSelection{};
    if(!db.ok()) {error="invalid frontend part database";return false;}
    // The row belongs to decoded FrontB chunk 0x30220; validate its identity
    // before consuming the fixed-size ordered slot table, including repeats.
    constexpr std::size_t offset=11949952,row_size=0x408;
    if(front.size()<offset+row_size ||
       std::memcmp(front.data()+offset+8,"SKYLINE\0",8)!=0 ||
       std::memcmp(front.data()+offset+0x28,"FE_SKYLINE\0",11)!=0) {
        error="original frontend Skyline preset absent";return false;
    }
    const auto car=bin_hash("SKYLINE");
    const auto lookup=[&](std::uint32_t type,int slot,std::uint32_t name) {
        return find_car_part(db,type,slot,name,kNoCarPart,kAnyUpgradeLevel);
    };
    for(unsigned pair=0;pair<kCarSlotCount;++pair) {
        const auto* p=front.data()+offset+0x48+pair*8;
        const auto slot=word(p),value=word(p+4);
        if(slot>=kCarSlotCount) {error="frontend preset slot outside RideInfo";return false;}
        // 0x4B2A4B does not assign hood decal slot 19.
        if(slot==19) continue;
        std::size_t record=kNoCarPart;
        if(slot==30 || (slot>=38 && slot<=57)) {
            if(value) record=lookup(0,30,value);
        } else if(slot>=31 && slot<=34) {
            // 0x4BC790 compares BINNAME attributes, not part-name hashes.
            for(const auto& part:db.records)
                if(part.slot==31 && attribute(db,part,0x10C98090U)==value) {
                    record=part.index;break;
                }
        } else if(slot>=59 && slot<=114) {
            if(value) record=lookup(0,58,value);
        } else if(slot==18) {
            record=lookup(bin_hash("PLATES"),18,value);
        } else if(slot==6) {
            if(value) record=lookup(car,6,value);
        } else if(slot==17) {
            record=lookup(bin_hash("WHEELS"),17,value);
            if(record==kNoCarPart) record=lookup(car,17,value);
        } else {
            record=lookup(car,static_cast<int>(slot),value);
            // Genuine callsites select these literal shared family names
            // before calling hash helper 0x567C70; no guessed ANY-car search.
            const char* family=slot==15?"EXHAUST":slot==16?"BRAKES":
                slot==115?"WINDOW_TINTS":slot==116?"NEON":nullptr;
            if(record==kNoCarPart && family)
                record=lookup(bin_hash(family),static_cast<int>(slot),value);
        }
        selection.records[slot]=record;
    }
    error.clear();return true;
}
}