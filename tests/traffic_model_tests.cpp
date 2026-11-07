#include "nfsu/traffic_model.h"
#include "nfsu/part_placement.h"
#include "nfsu/arg_path.h"
#include "traffic_model_native.inc"
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("retail root required");
        const auto root=nfsu::argument_path(argv[1]);
        const auto types=nfsu::load_car_type_table(root/"Global/GLOBALB.BUN");
        if(!types.ok()||types.records.size()!=35)throw std::runtime_error("retail car table");
        std::ifstream input(root/"Global/GLOBALB.BUN",std::ios::binary);
        std::array<nfsu::TrafficModelRecord,35> records{};
        std::array<unsigned char,0xc90> bytes{};
        for(const auto& type:types.records) {
            input.seekg(static_cast<std::streamoff>(type.offset));
            input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
            if(!input||!nfsu::decode_traffic_model_record(bytes.data(),bytes.size(),records.at(type.index))||
               records.at(type.index).id!=int(type.index))throw std::runtime_error("traffic decode");
        }
        std::uint32_t random=0;std::vector<int> used;unsigned index=0;
        for(const auto& row:kNativeTrafficModels) {
            if(row[2]==0) {used.clear();random=row[1];}
            if(random!=row[3])throw std::runtime_error("retained traffic random input");
            const auto choice=nfsu::choose_traffic_model(records.data(),records.size(),row[0],used.data(),used.size(),random);
            if(choice.id!=int(row[4])||choice.retries!=row[5]||choice.skin!=int(row[7])||random!=row[8])
                throw std::runtime_error("native traffic choice row "+std::to_string(index));
            used.push_back(choice.id);++index;
        }
        std::cout<<"PASS original weighted traffic, duplicate retries, skin/RNG: "<<index<<" rows\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
