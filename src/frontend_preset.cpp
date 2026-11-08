#include "nfsu/custom_vinyl.h"
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
bool resolve_frontend_preset_parts(const std::vector<unsigned char>& front,
    std::size_t offset,const std::string& car_name,const std::string& preset_name,
    const CarPartDatabase& db,InstalledPartSelection& selection,std::string& error) {
    selection=InstalledPartSelection{};
    if(!db.ok()) {error="invalid frontend part database";return false;}
    // The row belongs to decoded FrontB chunk 0x30220; validate its identity
    // before consuming the fixed-size ordered slot table, including repeats.
    constexpr std::size_t row_size=0x408;
    if(car_name.empty() || preset_name.empty() || car_name.size()>=32 || preset_name.size()>=32 ||
       offset>front.size() || row_size>front.size()-offset ||
       std::memcmp(front.data()+offset+8,car_name.c_str(),car_name.size()+1)!=0 ||
       std::memcmp(front.data()+offset+0x28,preset_name.c_str(),preset_name.size()+1)!=0) {
        error="original frontend preset identity mismatch";return false;
    }
    const auto car=bin_hash(car_name);
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

bool resolve_frontend_preset_skin(const std::vector<unsigned char>& globals,
    const std::string& car_name,const CarPartDatabase& db,const InstalledPartSelection& parts,
    BodySkinSelection& skin,std::string& error) {
    skin=BodySkinSelection{};
    const auto part=[&](unsigned slot)->const CarPartRecord* {
        const auto record=parts.records[slot];
        return record && *record!=kNoCarPart && *record<db.records.size() ? &db.records[*record] : nullptr;
    };
    const auto paint=[&](const CarPartRecord& p,std::uint32_t& result) {
        unsigned colors[4]{};const char* names[]={"RED","GREEN","BLUE","GLOSS"};
        for(unsigned k=0;k<4;++k) {
            bool present=false;
            for(int a=0;a<p.attribute_count;++a) {
                const auto& item=db.attributes[p.attribute_index+static_cast<unsigned>(a)];
                if(item.key==bin_hash(names[k])) {colors[k]=item.value;present=true;break;}
            }
            if(!present || colors[k]>255) return false;
        }
        result=skin_pack_paint(colors[0],colors[1],colors[2],colors[3]);return true;
    };
    const auto* base=part(30);
    if(!db.ok() || !base || !paint(*base,skin.base_color)) {
        error="original frontend base paint attributes absent";return false;
    }
    skin.car_name=car_name;
    skin.doorline=SkinDoorlineSelection{part(6)?SkinDoorlineVariant::wide_body:SkinDoorlineVariant::plain};
    for(unsigned k=0;k<4;++k) if(const auto* selected=part(37+k)) {
        std::uint32_t color=0;if(!paint(*selected,color)) {error="invalid frontend special paint";return false;}
        skin.special_colors[k]=color;
    }
    // Original part TEXTURE attributes index the database string table.
    // Locate its enclosing database, then require an in-bounds ASCII string.
    const unsigned char* strings=nullptr;std::size_t string_bytes=0;
    for(std::size_t at=0;at+8<=globals.size();) {
        const auto tag=word(globals.data()+at),size=word(globals.data()+at+4);
        if(size>globals.size()-at-8) {error="invalid frontend global chunk";return false;}
        if(tag==kCarPartDatabaseId) {
            for(std::size_t child=at+8;child+8<=at+8+size;) {
                const auto child_size=word(globals.data()+child+4);
                if(child_size>at+8+size-child-8) {error="invalid frontend part child";return false;}
                if(word(globals.data()+child)==kCarPartStringTableId) {
                    strings=globals.data()+child+8;string_bytes=child_size;
                }
                child+=8+child_size;
            }
        }
        at+=8+size;
    }
    if(!strings) {error="frontend part strings absent";return false;}
    for(unsigned layer=0;layer<4;++layer) if(const auto* selected=part(31+layer)) {
        if(selected->part_name_hash==kKrVinylPart){skin.kr_vinyl=true;continue;}
        bool duplicate=false;
        for(unsigned previous=0;previous<layer;++previous)
            duplicate=duplicate || part(31+previous)==selected;
        if(duplicate) continue; // 0x5A9F25 compares installed part identity.
        const auto offset=attribute(db,*selected,bin_hash("TEXTURE"));
        if(offset>=string_bytes) {error="frontend vinyl texture string outside database";return false;}
        SkinVinylLayerSelection vinyl;
        for(std::size_t k=offset;k<string_bytes && strings[k];++k) {
            if(strings[k]<32 || strings[k]>126) {error="invalid frontend vinyl texture name";return false;}
            vinyl.texture+=char(strings[k]);
        }
        if(vinyl.texture.empty() || offset+vinyl.texture.size()>=string_bytes) {
            error="frontend vinyl texture name absent";return false;
        }
        vinyl.palette_size=attribute(db,*selected,bin_hash("NUMCOLOURS"));
        vinyl.remap=attribute(db,*selected,bin_hash("REMAP"))!=0;
        if(vinyl.remap) for(unsigned k=0;k<4;++k) if(const auto* color_part=part(42+layer*4+k)) {
            std::uint32_t color=0;
            if(!paint(*color_part,color)) {error="invalid frontend remap paint";return false;}
            vinyl.remap_colors[k]=color;
        }
        skin.vinyl_layers[layer]=std::move(vinyl);
    }
    error.clear();return true;
}
bool resolve_frontend_skyline_parts(const std::vector<unsigned char>& front,
    const CarPartDatabase& db,InstalledPartSelection& selection,std::string& error) {
    return resolve_frontend_preset_parts(front,11949952,"SKYLINE","FE_SKYLINE",db,selection,error);
}
bool resolve_frontend_skyline_skin(const std::vector<unsigned char>& globals,
    const CarPartDatabase& db,const InstalledPartSelection& parts,
    BodySkinSelection& skin,std::string& error) {
    return resolve_frontend_preset_skin(globals,"SKYLINE",db,parts,skin,error);
}
bool resolve_frontend_sponsors(const CarPartDatabase& db,const StockParts& parts,
    FrontendSponsorHashes& hashes,std::string& error) {
    for(auto& panel:hashes) panel.fill(bin_hash("DEFAULTALPHA"));
    if(!db.ok()) {error="invalid frontend sponsor database";return false;}
    for(const auto& slot:parts.slots) if(slot.record!=kNoCarPart && slot.record>=db.records.size()) {
        error="frontend sponsor part outside database";return false;
    }
    const auto part=[&](unsigned slot)->const CarPartRecord* {
        const auto index=parts.slots[slot].record;
        return index!=kNoCarPart && index<db.records.size()?&db.records[index]:nullptr;
    };
    const auto attr=[&](const CarPartRecord& p,std::uint32_t key)->const CarPartAttribute* {
        for(int i=0;i<p.attribute_count;++i) {
            const auto& a=db.attributes[p.attribute_index+static_cast<unsigned>(i)];
            if(a.key==key)return &a;
        }
        return nullptr;
    };
    for(unsigned panel=0;panel<7;++panel) {
        const auto* rectangle=part(19+panel);
        if(!rectangle || (panel==0 && !part(12)))continue;
        const auto* size=attr(*rectangle,bin_hash("SIZE"));
        const auto* shape=attr(*rectangle,bin_hash("SHAPE"));
        if(!size || !shape)continue;
        const char* suffix=shape->value==bin_hash("SQUARE")?"_SQUARE":
            shape->value==bin_hash("RECT")?"_RECT":shape->value==bin_hash("WIDE")?"_WIDE":"";
        for(unsigned decal=0;decal<8;++decal) if(const auto* sponsor=part(59+panel*8+decal)) {
            const auto* name=attr(*sponsor,bin_hash("NAME"));
            if(!name)continue;
            auto hash=name->value;
            for(const char* c=suffix;*c;++c)hash=hash*33+static_cast<unsigned char>(*c);
            hashes[panel][decal]=hash;
        }
    }
    error.clear();return true;
}

}
