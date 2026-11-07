#include "nfsu/race_setup.h"
#include "nfsu/part_placement.h"
#include "nfsu/arg_path.h"
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=3)return 2;std::string error;std::filesystem::path root,out;
    if(!nfsu::argument_to_path(argv[1],root,error)||!nfsu::argument_to_path(argv[2],out,error)||std::filesystem::exists(out))return 2;
    std::filesystem::create_directories(out);
    const auto table=nfsu::load_car_type_table(root/"Global/GLOBALB.BUN");
    for(const auto* car:{"CIVIC","240SX","SKYLINE"}) {
        const auto id=nfsu::find_car_type(table,car);if(id==nfsu::kNoCarType)return 1;
        std::array<unsigned char,nfsu::kVehicleRecordSize> raw{},applied{};
        std::ifstream f(root/"Global/GLOBALB.BUN",std::ios::binary);f.seekg(table.records[id].offset);f.read(reinterpret_cast<char*>(raw.data()),raw.size());if(!f)return 1;
        const auto write=[&](const std::string& name,const auto& data) {std::ofstream g(out/name,std::ios::binary);g.write(reinterpret_cast<const char*>(data.data()),data.size());return bool(g);};
        if(!write(std::string(car)+"-original.bin",raw))return 1;
        for(int level=0;level<4;++level) {
            nfsu::VehicleUpgradeLevels l;l.weight=l.tyres=l.suspension=level;
            std::vector<nfsu::VehicleTuningIssue> issues;
            if(!nfsu::apply_vehicle_tuning(raw.data(),raw.size(),l,applied.data(),applied.size(),issues))return 1;
            const auto name=std::string(car)+"-"+std::to_string(level);
            if(!write(name+"-ordinary.bin",applied))return 1;
            if(!nfsu::apply_vehicle_drift_setup(raw.data(),raw.size(),l,applied.data(),applied.size())||!write(name+"-drift.bin",applied))return 1;
        }
    }
    std::cout<<"PASS drift setup samples written\n";
}
