#pragma once
#include "nfsu/car_assets.h"
#include "nfsu/race_gpu_scene.h"
#include "nfsu/stock_frame.h"
#include <filesystem>
#include <string>
namespace nfsu {
// The Eddie Skyline free-driving configuration: the original FE_SKYLINE
// preset of FrontEnd/FrontB.lzc resolved through the same resolvers the menu
// loader uses (resolve_frontend_skyline_parts/_skin), with the same
// post-preparation binds the menu applies (bind_frontend_skyline_runtime_skin,
// bind_frontend_sponsors). The dress's request keeps the race's own world
// pass and culling-table wheel placement, so driving keeps its brake LOD,
// lighting and wheel animation; only the authored configuration is shared
// with the menu. Not copyable: dress points into this object's own fields,
// so a copy's pointers would reach the original. Reset it by resolving into
// it again.
struct FreeDriveSkyline {
    FreeDriveSkyline() = default;
    FreeDriveSkyline(const FreeDriveSkyline&) = delete;
    FreeDriveSkyline& operator=(const FreeDriveSkyline&) = delete;
    CarAssets car;
    StockSceneRequest request;
    StockFrameSkin skin;
    RaceCarDress dress;
};
// Reads FrontB.lzc and GLOBALB.BUN read-only and fills `out`. Fails, with
// `error` set and nothing half-applied, when either image or the Skyline
// selection cannot be read or the original preset is absent - never by
// substituting another car or the stock configuration.
bool resolve_free_drive_skyline(const std::filesystem::path& root,
                                FreeDriveSkyline& out, std::string& error);
}
