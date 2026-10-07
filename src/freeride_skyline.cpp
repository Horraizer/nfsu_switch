#include "nfsu/freeride_skyline.h"
#include "nfsu/frontend_preset.h"
#include "nfsu/frontend_scene.h"
#include "nfsu/race_events.h"
#include <vector>
namespace nfsu {
namespace {
// The same post-preparation binds the menu applies, in its order: the
// GLOBAL_SKIN1 runtime-skin alias, then the per-panel sponsor substitution.
bool bind_free_drive_skyline(StockFrame& frame, const StockFrameInputs& inputs,
    const StockSceneRequest& request, std::string& error, void*) {
    return bind_frontend_skyline_runtime_skin(frame,request,error) &&
           bind_frontend_sponsors(inputs,request.installed_parts,frame,error);
}
}
bool resolve_free_drive_skyline(const std::filesystem::path& root,
                                FreeDriveSkyline& out, std::string& error) {
    out.car = CarAssets{};
    out.request = StockSceneRequest{};
    out.skin = StockFrameSkin{};
    out.dress = RaceCarDress{};
    // The same bounded FrontEnd read the menu's own loader makes, and the
    // default-limited GLOBALB read its preset tests make. Both buffers die
    // here; the request and skin below copy out of them.
    std::vector<unsigned char> front,globals;
    std::vector<RaceTableIssue> issues;
    RaceTableLimits limits; limits.max_file_size=32*1024*1024; limits.max_image_size=64*1024*1024;
    if(!load_race_image(root/"FrontEnd/FrontB.lzc",front,issues,limits)) {
        error="original frontend Skyline preset unreadable: FrontEnd/FrontB.lzc";return false;
    }
    if(!load_race_image(root/"Global/GLOBALB.BUN",globals,issues)) {
        error="original frontend part database unreadable: Global/GLOBALB.BUN";return false;
    }
    const auto database=parse_car_part_database(globals.data(),globals.size());
    InstalledPartSelection installed;
    if(!resolve_frontend_skyline_parts(front,database,installed,error)) return false;
    if(!load_car_assets(root/"Global/GLOBALB.BUN","SKYLINE",out.car,error)) return false;
    // stock_car_request keeps pass world and the culling-table wheel
    // placement: the genuine race LODs and the drive's wheel animation.
    out.request=stock_car_request("SKYLINE");
    out.request.installed_parts=installed;
    out.skin=stock_car_skin("SKYLINE");
    if(!resolve_frontend_skyline_skin(globals,database,installed,out.skin.selection,error)) return false;
    out.dress.request=&out.request;
    out.dress.skin=&out.skin;
    out.dress.bind=&bind_free_drive_skyline;
    error.clear();
    return true;
}
}
