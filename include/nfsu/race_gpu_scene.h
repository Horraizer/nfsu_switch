#pragma once

#include "nfsu/deko_gpfifo_budget.h"
#include "nfsu/race_run.h"
#include "nfsu/stock_frame.h"
#include "nfsu/platform.h"
#include "nfsu/race_particles.h"
#include "nfsu/race_skid.h"
#include "nfsu/car_shadow.h"
#include "nfsu/car_neon.h"
#include "nfsu/light_glows.h"
#include "nfsu/track_reflections.h"
#include "nfsu/headlights.h"
#include "nfsu/track_zones.h"
#include "nfsu/start_crowd.h"

namespace nfsu {
// One combined command stream: track then car, one camera and one depth
// buffer. Requires owning reference recordings; a borrowed race frame is
// already combined. Scene payloads stay stable until the renderer closes.
bool combine_race_commands(RecordedCommands track, const RecordedCommands& car,
                           RecordedCommands& out, std::string& error,
                           std::size_t resource_limit=kTrackSliceResourceLimit);
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
    void set_start_scenes_enabled(bool enabled) { start_scenes_enabled_ = enabled; }
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
    bool set_complete_zone_backing(bool enabled) {
        if(ready_)return false;
        complete_zone_backing_=enabled;return true;
    }
    std::size_t draw_capacity() const { return original_draw_distance_ ? kDekoPreparedDrawCapacity : kTrackSliceDrawLimit; }
    void set_race_visuals(bool enabled) { if(!ready_)race_visuals_=enabled; }
    void set_crash_windows(bool enabled) { if(!ready_)crash_windows_enabled_=enabled; }
    void set_silhouette_shadows(bool enabled) { if (!ready_) silhouette_shadows_ = enabled; }
    void set_low_car_reflections(bool enabled) noexcept {
        low_car_reflections_=enabled;
        cube_next_face_=0;
    }
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
              std::uint32_t event_number = 1, bool world_mips = false,unsigned race_index=0);
    bool set_start_scene(unsigned scene,const std::string& region) {
        if(ready_)return false;
        start_scene_=scene;start_region_=region;return true;
    }
    bool load_start_camera(FileSystemService& fs,const std::filesystem::path& root,unsigned scene,
                           const std::string& region,RaceStartClock& clock,std::string& error) {
        if(!ready_){error="race scene must load before start camera";return false;}
        if(start_scene_==scene && crowd_.actor_count()){clock=crowd_.clock();return true;}
        return crowd_.load_camera(fs,root,scene,region,clock,error);
    }
    bool load_finish_scene(FileSystemService&,const std::filesystem::path&,const RaceSession&,
                           const std::string& region,unsigned variant,std::string&);
    void clear_finish_scene() {finish_scene_.reset();}
    void set_finish_scene_seconds(double seconds) {finish_scene_seconds_=seconds;}
    bool frame(const RaceSession& session, RaceGpuFrame& out, std::string& error,
               RaceFrameProfile* profile = nullptr, bool reference = false,
               TimingService* timer = nullptr);
    // Append before the first frame/upload. Payloads move into this stable owner.
    bool add_rival(PreparedCarScene scene,std::vector<PreparedInstance> base,
                   std::size_t skin_source,CarShadowAsset shadow,std::string& error,bool traffic=false,
                   CarGlowAnchors anchors={},const VehicleHull* hull=nullptr,
                   std::unique_ptr<CarNeonAsset> neon={});
    std::uint64_t car_bytes() const { return car_bytes_; }
    // Conservative metadata/capacity estimate; payloads are borrowed once.
    std::uint64_t cache_bytes() const;
private:
    TrackSlice track_;
    struct AnimatedTexture {std::size_t animation=0,first=0;std::vector<std::size_t> frames;};
    std::vector<AnimatedTexture> animated_textures_;
    std::vector<DrawTextureAlias> texture_aliases_;
    std::size_t texture_alias_count() const override {return texture_aliases_.size();}
    DrawTextureAlias texture_alias(std::size_t i) const override {return texture_aliases_.at(i);}

    StartCrowd crowd_;
    std::unique_ptr<StartCrowd> finish_scene_;
    std::array<unsigned,3> finish_slots_{};
    double finish_scene_seconds_=0;
    unsigned start_scene_=0;
    bool start_scenes_enabled_=true;std::string start_region_;
    bool race_visuals_=false;
    bool original_draw_distance_ = false;
    TrackZoneReport visibility_zones_;
    std::vector<TrackResidency> zone_residency_;
    TrackResidency no_zone_{TrackResidencyRule::no_zone, {}};
    bool crash_windows_enabled_=false;
    CarCrashWindows crash_windows_;
    UploadTable crash_window_uploads_;
    std::array<std::size_t,2> crash_window_aliases_{{kNoHandle,kNoHandle}};
    std::size_t crash_window_resource_=kNoHandle;
    RenderTexture headlight_texture_;
    std::size_t headlight_resource_=kNoHandle;
    bool prepare_visual_frame(const RaceSession&, const VehicleState&, const CameraFrame&, RecordedCommands&, std::string&);
    WetRoadLamps lamps_;
    std::filesystem::path root_;
    bool lamps_loaded_ = false;
    RecordedCommands track_templates_;
    std::vector<RecordedDraw> world_draws_;
    std::vector<unsigned> world_draw_sources_;
    bool sky_ = true;
    bool complete_zone_backing_=false;
    std::size_t sky_draws_ = 0;
    std::size_t sky_position_ = 0;
    std::vector<std::size_t> template_of_group_;
    bool ready_ = false;
    bool tuned_ = false; // Visual selection only; never passed to RaceSession.
    bool dressed_ = false; // An explicit authored configuration was applied.
    TextureFilterMode texture_filter_ = TextureFilterMode::legacy;
    bool car_shading_ = false;
    bool low_car_reflections_ = false;
    unsigned cube_next_face_ = 0;
    bool wet_road_reflections_ = false;
    std::size_t resource_limit()const {
        return complete_zone_backing_?kTrackZoneBackingResourceLimit:kTrackSliceResourceLimit;
    }
    std::uint64_t cache_limit()const {return (complete_zone_backing_?32ULL:16ULL)*1024*1024;}
    bool prepare_templates(std::string& error);
    bool prepare_frame_cache(std::string& error);
    bool prepare_glows(std::string& error);
    bool select_track(const TrackCullReport& visible, RaceGpuFrame& out,
                      std::size_t& entries, std::string& error, bool capture = false);
    const UploadTable& resource_table() const override { return wet_road_reflections_ ? wet_uploads_ : combined_uploads_; }
    std::size_t selected_draw_count() const override {
        return sky_draws_ + selection_.size() + shadow_templates_.owning_draws().size() +
               car_templates_.owning_draws().size() + rival_draws_ + glow_count_ + crowd_.draw_count() + world_draws_.size();
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
    struct ReflectionInstance { std::size_t instance, first, end; };
    struct ReflectionSection { std::uint32_t number; std::size_t first, end; };
    std::vector<std::size_t> reflection_selection_;
    std::vector<ReflectionInstance> reflection_instances_;
    std::vector<ReflectionSection> reflection_sections_;
    std::vector<std::array<float, 6>> reflection_bounds_;
    std::vector<TrackReflectionCorners> reflection_corners_;
    bool prepare_reflections(std::string& error);
    bool select_reflections(const CameraFrame& camera, const TrackResidency* residency, std::string& error);
    void update_reflection_instance(std::size_t handle);
    PreparedTrackCull culler_;
    UploadTable combined_uploads_, wet_uploads_;
    RecordedCommands car_templates_;
    struct RivalVisual {
        bool traffic=false,active=false;
        float service_brake=0;
        CarGlowAnchors anchors{};
        OriginalCarGlows light_sprites;
        PreparedCarScene scene;
        std::vector<PreparedInstance> base;
        CarShadowAsset shadow;
        std::unique_ptr<CarNeonAsset> neon;
        std::unique_ptr<CarShadowSilhouette> silhouette;
        RecordedCommands commands,shadow_commands;
    };
    std::vector<RivalVisual> rivals_;
    std::size_t rival_draws_=0;
    RecordedCommands shadow_templates_;
    CarShadowAsset shadow_asset_;
    bool silhouette_shadows_ = false;
    RenderTexture shadow_ramp_;
    VehicleVec3 shadow_light_{};
    std::size_t shadow_ramp_resource_ = kNoHandle;
    std::unique_ptr<CarShadowSilhouette> shadow_silhouette_;
    CarNeonAsset neon_asset_;
    std::vector<std::size_t> mesh_first_, section_first_;
    std::vector<std::uint32_t> mesh_groups_;
    struct SelectedTrackDraw {
        std::uint32_t instance, draw;
        std::size_t ordinal, bucket_rank;
        unsigned char layer;
    };
    struct RetailGroup { std::size_t bucket = 0; unsigned char layer = 0; };
    std::vector<SelectedTrackDraw> selection_, cube_selection_, selection_scratch_, cube_scratch_;
    std::vector<RetailGroup> retail_groups_;
    std::vector<std::size_t> bucket_first_, bucket_cursor_;
    std::vector<unsigned char> bucket_layers_;
    std::vector<DrawInstanceUniform> track_placements_;
    std::vector<std::size_t> prop_handles_;
    std::vector<unsigned char> movable_instances_;
    bool movable_track_instance(std::size_t i) const override {return i<movable_instances_.size() && movable_instances_[i];}
    std::vector<std::pair<std::size_t,std::size_t>> moved_props_;
    std::uint64_t generation_ = 0;
    PreparedCarScene car_;
    std::vector<PreparedInstance> base_;
    std::size_t skin_source_ = kNoHandle;
    std::uint64_t car_bytes_ = 0;
    RaceParticles particles_;
    std::unique_ptr<RaceSkidMarks> skids_;
    bool particles_enabled_ = false;
    std::size_t car_draw_count_ = 0;
    bool glows_enabled_ = false;
    CarGlowAnchors glow_anchors_{};
    CarExhaustAnchors exhaust_anchors_{};
    RenderTexture nitrous_flame_,nitrous_purge_;
    std::array<MeshVertex,16> nitrous_vertices_{};
    std::array<std::uint16_t,24> nitrous_indices_{};
    RecordedDraw nitrous_template_,purge_template_;
    std::array<RecordedDraw,5> nitrous_selected_{};
    std::size_t nitrous_count_=0;
    RenderTexture glow_head_, glow_tail_;
    std::array<RenderTexture,3> original_glow_textures_;
    std::array<MeshVertex, 4> glow_vertices_{};
    std::array<std::uint16_t, 6> glow_indices_{{0,1,2,0,2,3}};
    std::array<RecordedDraw, 3> glow_templates_{};
    std::vector<RecordedDraw> glow_selected_;
    OriginalCarGlows light_sprites_;
    std::vector<RecordedDraw> road_glows_;
    std::vector<RecordedDraw> actor_glows_;
    std::size_t actor_glow_count_=0;
    std::size_t glow_count_ = 0;
    std::size_t glow_first_resource_ = 0;
};
} // namespace nfsu
