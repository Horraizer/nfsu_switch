#pragma once

#include "nfsu/part_placement.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// Data-driven car selection over the retail car type table (issue #315).
//
// The table is GLOBALB.BUN chunk 0x00034600, 35 records of 0xC90 bytes
// (include/nfsu/part_placement.h, docs/FORMATS.md). Each record names the car
// and the installation-relative path of its own geometry
// (`CARS\<NAME>\GEOMETRY.BIN` in the retail spelling), and its class field
// +0xC54 is 0 for the 20 playable racers and 2 for the 15 traffic-only
// vehicles, which this layer treats as the original classification.
//
// Nothing here reads a file other than the GLOBALB.BUN it is pointed at, and
// it writes nothing. The paths it returns are relative to an installation
// root, '/'-separated, in the spelling the pinned asset manifests use
// ("Cars/240SX/GEOMETRY.BIN").

// One resolved car. Every field comes from the car type record except where
// the comments say otherwise.
struct CarAssets {
    // The record's name (+0x00), which equals its lookup name (+0x20) in all
    // 35 retail records; the part database is keyed by it.
    std::string car_type_name;
    // bin_hash of car_type_name, the part lookup's key.
    std::uint32_t car_type_hash = 0;
    // The record's brand string.
    std::string brand_name;
    // The record's class field +0xC54: 0 racer, 2 traffic.
    std::uint32_t class_id = 0;
    // Index into CarTypeTable::records.
    std::size_t car_type = kNoCarType;
    // The record's absolute file offset; its first kVehicleRecordSize bytes
    // are the vehicle handling record the race decodes.
    std::uint64_t record_offset = 0;
    // The geometry path the record names, normalized to the installation's
    // relative form: '\' separators become '/', and the leading CARS segment
    // takes the installed spelling "Cars". The record spells the car's own
    // directory exactly as installed.
    std::string geometry_path;
    // The textures and vinyls of the same car. The record names only
    // geometry; these two are the same directory with TEXTURES.BIN and
    // VINYLS.BIN, the one triplet every one of the 39 retail Cars/*
    // directories holds. The runtime formatting of these two names was not
    // traced in speed.exe; this is the install-observed layout, and the
    // loaders fail with the path named if an installation ever differs.
    std::string textures_path;
    std::string vinyls_path;
};

// Resolves one car by name, byte for byte as find_car_type does. Fails, with
// `error` set and `car` untouched, for a table that is not valid, a name no
// record holds, or a record whose class is not 0: a traffic-only vehicle is
// not a selectable car, and the error names the class and says so rather than
// letting a traffic model stand in for a playable one.
bool resolve_car_assets(const CarTypeTable& table,
                        const std::string& car_type_name,
                        CarAssets& car,
                        std::string& error);

// Reads GLOBALB.BUN read-only, parses its car type table and resolves one
// car. The same failures as resolve_car_assets, plus the loader's own.
bool load_car_assets(const std::filesystem::path& global_bun,
                     const std::string& car_type_name,
                     CarAssets& car,
                     std::string& error);

// The playable racers of a valid table, in record order. Empty for a table
// that is not valid.
std::vector<std::string> playable_car_names(const CarTypeTable& table);

// True when the tuned preset of the stock frame (tuned_240sx_request/skin)
// is defined for this car. It is the 240SX's own part set, not a generic one,
// so it is available for the 240SX alone.
bool tuned_preset_available_for(const CarAssets& car) noexcept;

} // namespace nfsu
