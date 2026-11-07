#include "nfsu/car_assets.h"

#include "nfsu/texture.h"

#include <cctype>

namespace nfsu {

namespace {

// Splits a '/'-separated path into its non-empty segments.
std::vector<std::string> split_segments(const std::string& path) {
    std::vector<std::string> segments;
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const std::size_t end = path.find('/', begin);
        const std::string segment =
            path.substr(begin, end == std::string::npos ? end : end - begin);
        if (!segment.empty()) segments.push_back(segment);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return segments;
}

bool equals_case_insensitive(const std::string& text, const char* literal) {
    const std::string other(literal);
    if (text.size() != other.size()) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto a = static_cast<unsigned char>(text[i]);
        const auto b = static_cast<unsigned char>(other[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

} // namespace

static bool resolve_class_assets(unsigned expected_class,const CarTypeTable& table,
                        const std::string& car_type_name,
                        CarAssets& car,
                        std::string& error) {
    if (!table.ok()) {
        error = "the car type table is not valid";
        return false;
    }
    const std::size_t found = find_car_type(table, car_type_name);
    if (found == kNoCarType) {
        std::string names;
        for (const std::string& name : playable_car_names(table)) {
            if (!names.empty()) names += ", ";
            names += name;
        }
        error = "no car type is named '" + car_type_name +
                "'; the playable cars are: " + names;
        return false;
    }
    const CarTypeRecord& record = table.records[found];
    if (record.class_id != expected_class) {
        error = "'" + record.name + "' is a class " +
                std::to_string(record.class_id) +
                (expected_class==0 ? " vehicle, not a playable car" : " vehicle, not traffic");
        return false;
    }
    // The record spells its path with '\' separators and a leading CARS
    // segment: CARS\<NAME>\GEOMETRY.BIN. Normalize to the manifest spelling.
    std::string normalized = record.geometry_path;
    for (char& ch : normalized) {
        if (ch == '\\') ch = '/';
    }
    const std::vector<std::string> segments = split_segments(normalized);
    if (segments.size() < 2) {
        error = "car type '" + record.name + "' names a geometry path with "
                "no directory: '" + record.geometry_path + "'";
        return false;
    }
    std::string directory;
    for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
        if (!directory.empty()) directory += '/';
        // The record spells the leading segment CARS; the installation and
        // the pinned asset manifests spell it Cars. Every case-insensitive
        // file system this project runs on finds either, and the messages
        // that name the path read the same as the manifest's.
        directory += (i == 0 && equals_case_insensitive(segments[i], "CARS"))
                         ? std::string("Cars")
                         : segments[i];
    }
    car = CarAssets{};
    car.car_type_name = record.name;
    car.car_type_hash = bin_hash(record.name);
    car.brand_name = record.brand_name;
    car.class_id = record.class_id;
    car.car_type = found;
    car.record_offset = record.offset;
    car.geometry_path = directory + '/' + segments.back();
    car.textures_path = directory + "/TEXTURES.BIN";
    car.vinyls_path = directory + "/VINYLS.BIN";
    return true;
}

bool resolve_car_assets(const CarTypeTable& table,const std::string& name,
                        CarAssets& car,std::string& error) {
    return resolve_class_assets(0,table,name,car,error);
}
bool resolve_traffic_assets(const CarTypeTable& table,const std::string& name,
                            CarAssets& car,std::string& error) {
    return resolve_class_assets(2,table,name,car,error);
}

bool load_car_assets(const std::filesystem::path& global_bun,
                     const std::string& car_type_name,
                     CarAssets& car,
                     std::string& error) {
    const CarTypeTable table = load_car_type_table(global_bun);
    if (!table.ok()) {
        std::string first;
        if (!table.issues.empty()) {
            first = ": " + table.issues.front().message;
        }
        error = "the car type table of " + global_bun.generic_u8string() +
                " is not valid" + first;
        return false;
    }
    return resolve_car_assets(table, car_type_name, car, error);
}

std::vector<std::string> playable_car_names(const CarTypeTable& table) {
    std::vector<std::string> names;
    if (!table.ok()) return names;
    for (const CarTypeRecord& record : table.records) {
        if (record.class_id == 0) names.push_back(record.name);
    }
    return names;
}

bool tuned_preset_available_for(const CarAssets& car) noexcept {
    return car.car_type_name == "240SX";
}

} // namespace nfsu
