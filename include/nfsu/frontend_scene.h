#pragma once
#include "nfsu/draw_commands.h"
#include <filesystem>
#include <array>
#include <memory>
#include <string>
#include <vector>
namespace nfsu {
struct StockFrameInputs;
struct StockFrame;
struct InstalledPartSelection;
struct StockSceneRequest;
struct CareerRideSetup;
class CareerCatalog;
struct CareerProfile;
struct FrontendSceneEnvironment;
struct FrontendSceneCarSource;
bool bind_frontend_runtime_skin(StockFrame& car,
    const StockSceneRequest& request,std::string& error);
bool bind_frontend_skyline_runtime_skin(StockFrame& car,
    const StockSceneRequest& request,std::string& error);
// Native per-panel sponsor substitution, applied after the installed car and
// its runtime skin are prepared. Refuses conflicting mesh/panel ownership.
bool bind_frontend_sponsors(const StockFrameInputs& inputs,
    const InstalledPartSelection& installed,StockFrame& car,std::string& error);
// Resident original menu scene. Commands borrow its resources; this owner must
// outlive the GPU renderer. Frame updates change transforms/uniforms only.
// Unresolved drawable textures refuse GPU publication instead of checker art.
class FrontendGpuScene {
public:
    FrontendGpuScene();
    ~FrontendGpuScene();
    // Reload is transactional. Close every renderer borrowing commands first.
    bool load(const std::filesystem::path& root,unsigned width,unsigned height,std::string& error, const std::string& stock_car="",const CareerRideSetup* customized=nullptr);
    // Prepare the bounded starter roster once, before opening its renderer.
    // Selection only changes resident draws and original car-travel constants.
    bool load_browser(const std::filesystem::path& root,unsigned width,unsigned height,
                      const std::vector<std::string>& cars,std::string& error);
    bool select_car(std::size_t index,std::string& error);
    // Start native camera preparation while the incoming bank is loading.
    void prepare_car_change();
    void cancel_car_preparation();
    bool load_owned_ride(const std::filesystem::path& root,unsigned width,unsigned height,
                        const std::vector<unsigned char>& globals,const CareerCatalog&,const CareerProfile&,
                        std::string& error,std::shared_ptr<const FrontendSceneEnvironment> environment);
    // Same car, resident component variants; selection never runs car travel.
    bool load_garage(const std::filesystem::path& root,unsigned width,unsigned height,
                     const std::vector<CareerRideSetup>& variants,std::string& error,FrontendGpuScene* previous=nullptr,
                     std::shared_ptr<const FrontendSceneEnvironment> environment={},
                     std::shared_ptr<const FrontendSceneCarSource> source={});
    std::shared_ptr<const FrontendSceneEnvironment> environment() const;
    std::shared_ptr<const FrontendSceneCarSource> car_source() const;
    void release_preparation_cache();
    // Main-thread publication after a separate CPU scene has finished loading.
    // The prepared owner retains the old borrowed commands until GPU replacement.
    bool adopt_garage(FrontendGpuScene& prepared,std::string& error,std::size_t selection=0);
    bool select_part(std::size_t index,std::string& error);
    // Port right-stick mapping, using the original FE camera projection.
    void orbit(float yaw_degrees,float pitch_degrees);
    void focus_part(unsigned slot);
    void magazine_camera(const std::array<float,11>& camera);
    // Native garage constructors set20 km/h; rim browser50CD34 sets1.
    void road_target(float kmh);
    std::size_t draw_capacity() const;
    bool car_transitioning() const;
    const std::string& selected_car_type() const;
    bool frame(float seconds,std::uint32_t ticks,std::string& error);
    const RecordedCommands& commands() const;
    // Diagnostic CPU rendering of the current road/wheel command placements.
    // Uses the reference material implementation; does not certify FE effects.
    bool render_reference(std::vector<unsigned char>& rgba,std::string& error) const;
private:
    bool load_cached(const std::filesystem::path&,unsigned,unsigned,std::string&,
        const std::string&,const CareerRideSetup*,std::shared_ptr<const FrontendSceneEnvironment>,bool vehicle_only,
        std::shared_ptr<const FrontendSceneCarSource> source={},struct StockFramePaintCache* paint_cache=nullptr);
    bool load_choices(const std::filesystem::path&,unsigned,unsigned,
        const std::vector<std::string>&,const std::vector<CareerRideSetup>*,std::string&,FrontendGpuScene* previous=nullptr,std::shared_ptr<const FrontendSceneEnvironment> environment={},
        std::shared_ptr<const FrontendSceneCarSource> source={});
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
bool load_frontend_scene(const std::filesystem::path& root,
    std::vector<unsigned char>& rgba, unsigned width, unsigned height,
    std::string& error);
}
