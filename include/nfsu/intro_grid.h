#pragma once
#include "nfsu/event0_setup.h"
#include "nfsu/car_shadow.h"
#include "nfsu/car_neon.h"
#include "nfsu/light_glows.h"
#include <memory>
namespace nfsu {
struct RaceCarDress;
// Visual starting grid only. No rival performance or controller is invented.
// Rival physical specs include original adaptive performance matching.
struct IntroGridActor {
    Event0ActorConfig config;
    CarAssets assets;
    StockSceneRequest request;
    StockFrameSkin skin;
    std::unique_ptr<StockFrame> frame;
    VehicleSpec placement_spec;
    VehicleHull hull;
    VehicleState state;
    VehiclePose pose;
    std::vector<PreparedInstance> base_instances;
    CarShadowAsset shadow;
    std::unique_ptr<CarNeonAsset> neon;
    CarGlowAnchors glow_anchors{};
};
struct IntroGrid {
    std::shared_ptr<const Event0SetupDescriptor> setup;
    VehicleTrackCourse course;
    std::vector<IntroGridActor> traffic;
    std::vector<IntroGridActor> actors; // Final physical grid order.
};
// Authenticates retail setup, installs each authored preset, projects grid markers
// to the course, and prepares stable owned car payloads. Publishes only on success.
bool load_intro_grid(const std::filesystem::path& root,IntroGrid& out,std::string& error);
enum class GridPlayerVisuals { prepare, load_with_scene };
bool load_race_grid(const std::filesystem::path& root,unsigned event,const std::string& car,
                     unsigned difficulty,IntroGrid& out,std::string& error,const VehicleUpgradeLevels* owned_levels=nullptr,
                     const RaceCarDress* owned_dress=nullptr,unsigned race_index=0,
                     GridPlayerVisuals player_visuals=GridPlayerVisuals::prepare,const QuickRaceOptions* quick=nullptr);
bool load_quick_race_traffic(const std::filesystem::path&,const SupportedRaceSelection&,
    unsigned density,std::vector<IntroGridActor>&,std::string& error);
// Common real roster/session setup; planner and driver own live AI behavior.
bool prepare_intro_race(IntroGrid&,RaceSession&,std::string& error);
}
