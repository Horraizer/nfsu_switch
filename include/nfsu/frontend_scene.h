#pragma once
#include "nfsu/draw_commands.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace nfsu {
struct StockFrameInputs;
struct StockFrame;
struct InstalledPartSelection;
struct StockSceneRequest;
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
    bool load(const std::filesystem::path& root,unsigned width,unsigned height,std::string& error);
    bool frame(float seconds,std::uint32_t ticks,std::string& error);
    const RecordedCommands& commands() const;
    // Diagnostic CPU rendering of the current road/wheel command placements.
    // Uses the reference material implementation; does not certify FE effects.
    bool render_reference(std::vector<unsigned char>& rgba,std::string& error) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
bool load_frontend_scene(const std::filesystem::path& root,
    std::vector<unsigned char>& rgba, unsigned width, unsigned height,
    std::string& error);
}
