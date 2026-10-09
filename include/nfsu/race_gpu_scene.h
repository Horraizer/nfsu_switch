#pragma once
#include "nfsu/race_run.h"
#include "nfsu/stock_frame.h"
#include "nfsu/platform.h"
#include "nfsu/race_particles.h"
#include "nfsu/car_shadow.h"
#include "nfsu/light_glows.h"
#include "nfsu/track_reflections.h"
#include "nfsu/headlights.h"
#include "nfsu/track_zones.h"

namespace nfsu {
// One combined command stream: track then car, one camera and one depth
// buffer. Requires owning reference recordings; a borrowed race frame is
// already combined. Scene payloads stay stable until the renderer closes.
bool combine_race_commands(RecordedCommands track, const RecordedCommands& car,
                           RecordedCommands& out, std::string& error);
struct RaceGpuFrame {
    RecordedCommands commands;
    std::size_t track_draws = 0, car_draws = 0, glow_draws = 0, visible = 0;
    std::size_t particle_draws = 0;
    std::uint64_t triangles = 0, unplaced = 0;
    double far_plane = 0;
};
struct RaceFrameProfile {
    double cull_ms = 0, list_ms = 0, car_ms = 0, track_ms = 0, combine_ms = 0;
    // Children of combine_ms, not additional parent costs.
    double road_reflections_ms = 0, race_visuals_ms = 0;
};
// An explicit visual configuration for the racer this scene draws, supplied by
// a caller that links the layer which authored it. The request and skin
// replace the stock/tuned presets load() would pick; `bind` runs after
// prepare_race_car_frame, while the frame and its inputs are still alive, so
// the authoring layer can apply its own post-preparation binds (the frontend
// Skyline's runtime-skin alias and sponsor substitution). The load refuses a
// dress whose request names another car than the selected one, so an authored
// part set never silently dresses a different body. A dress implies the tuned
// input extras (wheel geometry, wheel textures and the car's vinyls).
struct RaceCarDress {
    const StockSceneRequest* request = nullptr;
    const StockFrameSkin* skin = nullptr;
    bool (*bind)(StockFrame& frame, const StockFrameInputs& inputs,
                 const StockSceneRequest& request, std::string& error,
                 void* user) = nullptr;
    void* user = nullptr;
};
class RaceGpuScene : private PreparedCommandSource {
public:
    RaceGpuScene() = default;
    RaceGpuScene(const RaceGpuScene&) = delete;
    RaceGpuScene& operator=(const RaceGpuScene&) = delete;
    const TrackSlice& track() const { return track_; }
    // Select before load. Both values use the same executable and assets.
    bool set_texture_filter(TextureFilterMode mode) {
        if (ready_ || (mode != TextureFilterMode::legacy && mode != TextureFilterMode::linear)) return false;
        texture_filter_ = mode; return true;
    }
    void set_particles_enabled(bool enabled) { particles_enabled_ = enabled; }
    void advance_particles(const RaceSession& session) noexcept {
        if (particles_enabled_) particles_.advance(session);
    }
    // The separate sky pass (issue 184), on by default. Off records no sky
    // draws, for console A/B cost runs. Refused once the scene is loaded.
    bool set_sky(bool enabled) { if (ready_) return false; sky_ = enabled; return true; }
    bool set_original_draw_distance(bool enabled) {
        if (ready_) return false;
        original_draw_distance_ = enabled; return true;
    }
    std::size_t draw_capacity() const { return original_draw_distance_ ? 4096 : kTrackSliceDrawLimit; }
    void set_race_visuals(bool enabled) { if(!ready_)race_visuals_=enabled; }
    void set_wet_road_reflections(bool enabled) noexcept { wet_road_reflections_ = enabled; }
    // `car` selects the racer every path draws and drives (issue #315); null
    // resolves the default 240SX through the retail car type table. The
    // tuned visual preset stays the 240SX's own and is refused for any other
    // car rather than silently substituted. `dress` replaces that preset with
    // an explicit authored configuration; it is exclusive with `tuned`.
    bool load(FileSystemService& files, const std::filesystem::path& root,
              std::string& error, AssetCheckObserver* observer = nullptr,
              TrackSliceLoadObserver* loading = nullptr, bool tuned = false,
              bool glows = false, bool car_shading = false,
              const CarAssets* car = nullptr, const RaceCarDress* dress = nullptr,
              std::uint32_t event_number = 1, bool world_mips = false);
    bool frame(const RaceSession& session, RaceGpuFrame& out, std::string& error,
               RaceFrameProfile* profile = nullptr, bool reference = false,
               TimingService* timer = nullptr);
    std::uint64_t car_bytes() const { return car_bytes_; }
    // Conservative metadata/capacity estimate; payloads are borrowed once.
    std::uint64_t cache_bytes() const;
private:
    TrackSlice track_;
    bool race_visuals_=false;
    bool original_draw_distance_ = false;
    TrackZoneReport visibility_zones_;
    std::vector<TrackResidency> zone_residency_;
    TrackResidency no_zone_{TrackResidencyRule::no_zone, {}};
    RenderTexture headlight_texture_;
    std::size_t headlight_resource_=kNoHandle;
    bool prepare_visual_frame(const RaceSession&, const CameraFrame&, RecordedCommands&, std::string&);
    WetRoadLamps lamps_;
    std::filesystem::path root_;
    bool lamps_loaded_ = false;
    RecordedCommands track_templates_;
    bool sky_ = true;
    std::size_t sky_draws_ = 0;
    std::size_t sky_position_ = 0;
    std::vector<std::size_t> template_of_group_;
    bool ready_ = false;
    bool tuned_ = false; // Visual selection only; never passed to RaceSession.
    bool dressed_ = false; // An explicit authored configuration was applied.
    TextureFilterMode texture_filter_ = TextureFilterMode::legacy;
    bool car_shading_ = false;
    bool wet_road_reflections_ = false;
    bool prepare_templates(std::string& error);
    bool prepare_frame_cache(std::string& error);
    bool prepare_glows(std::string& error);
    bool select_track(const TrackCullReport& visible, RaceGpuFrame& out,
                      std::size_t& entries, std::string& error, bool capture = false);
    const UploadTable& resource_table() const override { return wet_road_reflections_ ? wet_uploads_ : combined_uploads_; }
    std::size_t selected_draw_count() const override {
        return sky_draws_ + selection_.size() + 1 +
               car_templates_.owning_draws().size() + glow_count_;
    }
    RecordedDrawView selected_draw(std::size_t i) const override;
    std::uint64_t command_generation() const override { return generation_; }
    std::size_t static_track_draw_count() const override { return sky_draws_ + selection_.size(); }
    std::size_t static_track_instance_count() const override { return track_placements_.size(); }
    const DrawInstanceUniform& static_track_instance(std::size_t i) const override { return track_placements_.at(i); }
    std::size_t static_track_definition_count() const override { return track_templates_.owning_draws().size(); }
    const RecordedDraw& static_track_definition(std::size_t i) const override { return track_templates_.owning_draws().at(i); }
    std::size_t reflection_draw_count() const override { return reflection_selection_.size() + lamp_draws_.size(); }
    const RecordedDraw& reflection_draw(std::size_t i) const override { return i < reflection_selection_.size() ? reflection_templates_.at(reflection_selection_[i]) : lamp_draws_.at(i-reflection_selection_.size()); }
    std::vector<RecordedDraw> reflection_templates_, lamp_draws_;
    std::vector<std::size_t> reflection_first_, reflection_selection_;
    std::vector<TrackDrawListEntry> reflection_entries_;
    TrackReflectionCache reflection_cache_;
    bool prepare_reflections(std::string& error);
    PreparedTrackCull culler_;
    UploadTable combined_uploads_, wet_uploads_;
    RecordedCommands car_templates_;
    RecordedCommands shadow_templates_;
    CarShadowAsset shadow_asset_;
    std::vector<std::size_t> mesh_first_, section_first_;
    std::vector<std::uint32_t> mesh_groups_;
    struct SelectedTrackDraw { std::uint32_t instance, draw; std::size_t ordinal; };
    struct RetailGroup { std::size_t bucket = 0; unsigned char layer = 0; };
    std::vector<SelectedTrackDraw> selection_, cube_selection_;
    std::vector<RetailGroup> retail_groups_;
    std::vector<std::size_t> bucket_first_;
    std::vector<DrawInstanceUniform> track_placements_;
    std::uint64_t generation_ = 0;
    PreparedCarScene car_;
    std::vector<PreparedInstance> base_;
    std::size_t skin_source_ = kNoHandle;
    std::uint64_t car_bytes_ = 0;
    RaceParticles particles_;
    bool particles_enabled_ = false;
    std::size_t car_draw_count_ = 0;
    bool glows_enabled_ = false;
    CarGlowAnchors glow_anchors_{};
    CarExhaustAnchors exhaust_anchors_{};
    RenderTexture nitrous_flame_;
    std::array<MeshVertex,16> nitrous_vertices_{};
    std::array<std::uint16_t,24> nitrous_indices_{};
    RecordedDraw nitrous_template_;
    std::array<RecordedDraw,3> nitrous_selected_{};
    std::size_t nitrous_count_=0;
    RenderTexture glow_head_, glow_tail_;
    std::array<RenderTexture,3> original_glow_textures_;
    std::array<MeshVertex, 4> glow_vertices_{};
    std::array<std::uint16_t, 6> glow_indices_{{0,1,2,0,2,3}};
    std::array<RecordedDraw, 3> glow_templates_{};
    std::array<RecordedDraw, 7> glow_selected_{};
    std::vector<RecordedDraw> road_glows_;
    std::size_t glow_count_ = 0;
    std::size_t glow_first_resource_ = 0;
};
} // namespace nfsu
