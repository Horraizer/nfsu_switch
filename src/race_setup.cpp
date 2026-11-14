#include "nfsu/race_setup.h"
#include "nfsu/car_assets.h"
#include "nfsu/part_placement.h"
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "nfsu/ai_powertrain.h"

namespace nfsu {
bool load_race_camera_table(const std::filesystem::path& global, const CarAssets& car,
                            ChaseCameraTable& table, std::string& error) {
    std::array<unsigned char,kVehicleRecordSize> record{};
    std::ifstream input(global,std::ios::binary);
    input.seekg(static_cast<std::streamoff>(car.record_offset));
    input.read(reinterpret_cast<char*>(record.data()),record.size());
    if(!input) {
        error="camera record unavailable for "+car.car_type_name;
        return false;
    }
    return decode_chase_camera_table(record.data(),record.size(),table,error);
}
bool race_rival_throttle_cap(unsigned selector,float& cap) noexcept {
    static constexpr float values[]{.93F,.97F,1.F};
    if(selector>=3)return false;
    cap=values[selector];return true;
}
namespace {
struct LevelField { const char* name; int VehicleUpgradeLevels::*member; };
constexpr LevelField fields[] = {
    {"weight", &VehicleUpgradeLevels::weight},
    {"suspension", &VehicleUpgradeLevels::suspension},
    {"engine-a", &VehicleUpgradeLevels::engine_a},
    {"induction", &VehicleUpgradeLevels::induction},
    {"nitrous", &VehicleUpgradeLevels::nitrous},
    {"engine-b", &VehicleUpgradeLevels::engine_b},
    {"gearbox", &VehicleUpgradeLevels::gearbox},
    {"tyres", &VehicleUpgradeLevels::tyres},
    {"grip", &VehicleUpgradeLevels::grip},
    {"aero-a", &VehicleUpgradeLevels::aero_a},
    {"aero-b", &VehicleUpgradeLevels::aero_b}};
}
bool parse_performance_levels(const std::string& text, VehicleUpgradeLevels& levels,
                              std::string& error) {
    VehicleUpgradeLevels next;
    unsigned seen = 0;
    if (text.empty() || text.size() > 256) { error = "empty or oversized performance levels"; return false; }
    std::size_t begin = 0;
    do {
        const auto end = text.find(',', begin);
        const auto token = text.substr(begin, end == std::string::npos ? end : end-begin);
        const auto equals = token.find('=');
        if (equals == std::string::npos || equals+2 != token.size() ||
            token.back() < '0' || token.back() > '3') {
            error = "performance levels require name=0..3"; return false;
        }
        bool found = false;
        for (unsigned i = 0; i < sizeof(fields)/sizeof(*fields); ++i) {
            if (token.substr(0, equals) != fields[i].name) continue;
            if (seen & (1U<<i)) { error = "duplicate performance level"; return false; }
            seen |= 1U<<i; next.*fields[i].member = token.back()-'0'; found = true;
        }
        if (!found) { error = "unsupported performance level: " + token.substr(0,equals); return false; }
        if (end == std::string::npos) break;
        begin = end+1;
    } while (begin <= text.size());
    levels = next; return true;
}
std::string performance_levels_text(const VehicleUpgradeLevels& levels) {
    std::string result;
    for (const auto& field : fields) {
        if (!result.empty()) result += ',';
        result += field.name; result += '='; result += std::to_string(levels.*field.member);
    }
    return result;
}
VehicleUpgradeLevels performance1_levels() {
    VehicleUpgradeLevels levels;
    levels.engine_a=levels.gearbox=levels.weight=levels.suspension=levels.tyres=1;
    return levels;
}
bool performance_levels_equal(const VehicleUpgradeLevels& a, const VehicleUpgradeLevels& b) {
    for (const auto& field : fields) if (a.*field.member != b.*field.member) return false;
    return a.mass_item==b.mass_item && a.torque_item_a==b.torque_item_a &&
        a.torque_item_b==b.torque_item_b && a.torque_item_c==b.torque_item_c &&
        a.nitrous_item==b.nitrous_item && a.drive_split_item==b.drive_split_item &&
        a.tyre_friction_item==b.tyre_friction_item && a.grip_item==b.grip_item;
}
bool make_race_vehicle_spec(const unsigned char* original, std::size_t size,
                            const VehicleUpgradeLevels& levels, VehicleSpec& spec,
                            std::string& error,bool drift,unsigned difficulty) {
    if(difficulty>2){error="invalid drift difficulty";return false;}
    std::array<unsigned char,kVehicleRecordSize> applied{};
    std::vector<VehicleTuningIssue> issues;
    //57F2B0 clears both visual aero levels before575960. Normal live
    // setups derive aero continuously from weight/suspension/tyres below;
    // the authored individual-part tables remain available in the raw API.
    auto runtime_levels=levels;
    if(levels.aero_a<0||levels.aero_a>3||levels.aero_b<0||levels.aero_b>3) {
        error="performance setup: invalid aero level";return false;
    }
    runtime_levels.aero_a=runtime_levels.aero_b=0;
    if (!apply_vehicle_tuning(original,size,runtime_levels,applied.data(),applied.size(),issues)) {
        error = "performance setup: " + (issues.empty() ? std::string("apply refused") : issues.front().message);
        return false;
    }
    const auto scalar=[](const unsigned char* at){float f;std::memcpy(&f,at,4);return f;};
    // Runtime 576250..57635A uses binary32. 57F2B0 then blends only the
    // drift mass toward stock, leaving the already-built inertia matrix.
    float inertia_mass=scalar(applied.data()+kVehicleMassOffset);
    if(levels.mass_item) {
        VehicleTuning tuning;
        if(!decode_vehicle_tuning(original,size,tuning,issues)) {
            error="runtime mass tuning refused";return false;
        }
        const float scale=(levels.weight>0?tuning.weight[levels.weight-1].mass_scale:1.F)
            -tuning.item.mass_scale_bonus;
        inertia_mass=scale*scalar(original+kVehicleMassOffset);
        std::memcpy(applied.data()+kVehicleMassOffset,&inertia_mass,4);
    }
    if(drift && !apply_vehicle_drift_setup(original,size,levels,applied.data(),applied.size())) {
        error="drift setup refused";return false;
    }
    VehicleSpec next;next.drift_mode=drift;next.drift_difficulty=difficulty;
    std::vector<VehicleHandlingIssue> handling_issues;
    if (!decode_vehicle_handling(applied.data(),applied.size(),next.handling,handling_issues)) {
        error = "performance handling: " + handling_issues.front().message; return false;
    }
    auto& h=next.handling;
    const float l2=h.length*h.length,w2=h.width*h.width,h2=h.height*h.height;
    h.inertia={((h2+w2)*inertia_mass)*0.083333335F,
               ((h2+l2)*inertia_mass)*0.083333335F,
               ((w2+l2)*inertia_mass)*0.083333335F};
    const auto word=[&](std::size_t at) {
        return std::uint32_t(applied[at]) | std::uint32_t(applied[at+1])<<8 |
               std::uint32_t(applied[at+2])<<16 | std::uint32_t(applied[at+3])<<24;
    };
    const auto real=[&](std::size_t at) { const auto bits=word(at);float f;std::memcpy(&f,&bits,4);return f; };
    if(!drift) {
        const float normalized=float(levels.weight+levels.suspension+levels.tyres)*.1111111119389534F;
        h.downforce_front+=((scalar(original+0x7d0)+scalar(original+0x800))*normalized);
        h.downforce_rear+=((scalar(original+0x7d4)+scalar(original+0x804))*normalized);
        h.aero_preload+=((scalar(original+0x7d8)+scalar(original+0x808))*normalized);
    }
    for (unsigned k=0;k<9;++k) next.induction[k]=real(0x334+4*k);
    next.nitrous.installed=!drift&&levels.nitrous>0;
    if (next.nitrous.installed) {
        next.nitrous.capacity_seconds=real(0x3B0);
        next.nitrous.count=static_cast<std::int32_t>(word(0x3B4));
        next.nitrous.rate=real(0x3BC);
    }
    const VehicleModel model(next);
    if (!model.valid()) { error = model.error(); return false; }
    spec = next; return true;
}
bool load_race_vehicle_spec(const std::filesystem::path& global, const std::string& car,
                            const VehicleUpgradeLevels& levels, VehicleSpec& spec,
                            std::string& error,bool drift,unsigned difficulty) {
    const auto table = load_car_type_table(global);
    const auto index = find_car_type(table,car);
    if (!table.ok() || index == kNoCarType) { error = "performance setup: invalid or missing car record"; return false; }
    std::array<unsigned char,kVehicleRecordSize> original{};
    std::ifstream in(global,std::ios::binary);
    in.seekg(static_cast<std::streamoff>(table.records[index].offset));
    in.read(reinterpret_cast<char*>(original.data()),original.size());
    if (!in || in.gcount() != static_cast<std::streamsize>(original.size())) {
        error = "performance setup: short car record read"; return false;
    }
    return make_race_vehicle_spec(original.data(),original.size(),levels,spec,error,drift,difficulty);
}
#include "race_adaptive.inc"
} // namespace nfsu
