#include "nfsu/quick_rival.h"
#include "nfsu/car_assets.h"
#include "nfsu/race_events.h"
#include "nfsu/race_setup.h"
#include "nfsu/arg_path.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include "../tests/quick_visual_native.inc"
#include "../tests/quick_context_native.inc"
int main(int argc,char** argv) {
    try {
        if(argc<2||argc>3)throw std::runtime_error("usage: quick_rival_probe retail-root");
        const auto root=nfsu::argument_path(argv[1]);
        std::vector<unsigned char> data;std::vector<nfsu::RaceTableIssue> issues;
        if(!nfsu::load_race_image(root/"Global/GLOBALB.BUN",data,issues))throw std::runtime_error("GLOBALB unreadable");
        const auto db=nfsu::parse_car_part_database(data.data(),data.size());
        const auto types=nfsu::parse_car_type_table(data.data(),data.size());
        if(argc==3&&std::string(argv[2])=="--verify") {
            for(const auto& test:quick_visual_native) {
                auto seed=test.seed;nfsu::VehicleUpgradeLevels levels;
                const char* names[]={"weight","suspension","engine-a","induction","nitrous","engine-b","gearbox","tyres","grip","aero-a","aero-b"};
                std::string text,error;for(unsigned i=0;i<11;++i){if(i)text+=',';text+=std::string(names[i])+"="+std::to_string(test.levels[i]);}
                if(!nfsu::parse_performance_levels(text,levels,error))throw std::runtime_error(error);
                const auto index=nfsu::find_car_type(types,test.car);
                if(index==nfsu::kNoCarType)throw std::runtime_error("native witness car missing");
                nfsu::InstalledPartSelection selected;
                if(!nfsu::quick_rival_visual(db,test.car,data.data()+types.records[index].offset,nfsu::kVehicleRecordSize,levels,seed,selected,error))throw std::runtime_error(error);
                nfsu::StockParts parts;if(!nfsu::select_installed_parts(db,test.car,selected,parts,error))throw std::runtime_error(error);
                if(seed!=test.rng)throw std::runtime_error("native visual RNG differs");
                for(unsigned i=0;i<120;++i) {
                    const auto value=parts.slots[i].record==nfsu::kNoCarPart?-1:int(parts.slots[i].record);
                    if(value!=test.slots[i]||parts.slots[i].enabled!=(test.enabled[i]=='1'))
                        throw std::runtime_error("native visual slot differs: "+std::string(test.car)+" "+std::to_string(i));
                }
            }
            const unsigned char* ai=nullptr;
            const auto word=[](const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);};
            for(std::size_t at=0;at+8<=data.size();) {
                const auto tag=word(data.data()+at),bytes=word(data.data()+at+4);at+=8;
                if(bytes>data.size()-at)throw std::runtime_error("GLOBALB chunk extent");
                if(tag==0x34b00&&bytes==0x82c)ai=data.data()+at;
                at+=bytes;
            }
            if(!ai)throw std::runtime_error("quick context table missing");
            for(const auto& test:quick_context_native) {
                auto seed=test[1];const auto context=nfsu::quick_rival_context(ai,test[0],seed);
                if(context!=int(test[2])||seed!=test[3])throw std::runtime_error("original4B4590 context differs");
            }
            const auto index=nfsu::find_car_type(types,"CIVIC");
            if(index==nfsu::kNoCarType)throw std::runtime_error("Civic validation record missing");
            nfsu::InstalledPartSelection ignored;nfsu::VehicleUpgradeLevels invalid;invalid.engine_a=4;
            std::string error;std::uint32_t seed=0;
            if(nfsu::quick_rival_visual(db,"CIVIC",data.data()+types.records[index].offset,nfsu::kVehicleRecordSize,invalid,seed,ignored,error))
                throw std::runtime_error("invalid appearance level accepted");
            std::cout<<"PASS original57D860 installed slots, compatibility and RNG; original4B4590 contexts\n";return 0;
        }
        std::string car;unsigned seed=0;
        while(std::cin>>car>>seed) {
            std::string text;std::cin>>text;nfsu::VehicleUpgradeLevels levels;std::string error;
            if(!nfsu::parse_performance_levels(text,levels,error))throw std::runtime_error(error);
            const auto it=std::find_if(types.records.begin(),types.records.end(),[&](const auto& t){return t.name==car;});
            if(it==types.records.end())throw std::runtime_error("car missing");
            nfsu::InstalledPartSelection selected;
            if(!nfsu::quick_rival_visual(db,car,data.data()+it->offset,nfsu::kVehicleRecordSize,levels,seed,selected,error))throw std::runtime_error(error);
            nfsu::StockParts parts;if(!nfsu::select_installed_parts(db,car,selected,parts,error))throw std::runtime_error(error);
            std::cout<<"VISUAL "<<seed;
            for(const auto& slot:parts.slots)std::cout<<' '<<(slot.record==nfsu::kNoCarPart?-1:static_cast<int>(slot.record));
            for(const auto& slot:parts.slots)std::cout<<' '<<slot.enabled;
            std::cout<<'\n';
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
