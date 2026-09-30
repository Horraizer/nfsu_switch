#pragma once
#include "nfsu/race_run.h"
#include "nfsu/stock_frame.h"
#include "nfsu/platform.h"

namespace nfsu {
// One combined command stream: track then car, one camera and one depth
// buffer. Requires owning reference recordings; a borrowed race frame is
// already combined. Scene payloads stay stable until the renderer closes.
bool combine_race_commands(RecordedCommands track, const RecordedCommands& car,
                           RecordedCommands& out, std::string& error);
struct RaceGpuFrame {
    RecordedCommands commands;
    std::size_t track_draws = 0, car_draws = 0, visible = 0;
    std::uint64_t triangles = 0, unplaced = 0;
    double far_plane = 0;
};
struct RaceFrameProfile {
    double cull_ms = 0, list_ms = 0, car_ms = 0, track_ms = 0, combine_ms = 0;
};
class RaceGpuScene : private PreparedCommandSource {
public:
    RaceGpuScene() = default;
    RaceGpuScene(const RaceGpuScene&) = delete;
    RaceGpuScene& operator=(const RaceGpuScene&) = delete;
    const TrackSlice& track() const { return track_; }
    // The separate sky pass (issue 184), on by default. Off records no sky
    // draws, for console A/B cost runs. Refused once the scene is loaded.
    bool set_sky(bool enabled) { if (ready_) return false; sky_ = enabled; return true; }
    bool load(FileSystemService& files, const std::filesystem::path& root,
              std::string& error, AssetCheckObserver* observer = nullptr,
              TrackSliceLoadObserver* loading = nullptr, bool car_shading = false);
    bool frame(const RaceSession& session, RaceGpuFrame& out, std::string& error,
               RaceFrameProfile* profile = nullptr, bool reference = false,
               TimingService* timer = nullptr);
    std::uint64_t car_bytes() const { return car_bytes_; }
    // Conservative metadata/capacity estimate; payloads are borrowed once.
    std::uint64_t cache_bytes() const;
private:
    TrackSlice track_;
    RecordedCommands track_templates_;
    bool sky_ = true;
    std::size_t sky_draws_ = 0;
    std::size_t sky_position_ = 0;
    std::vector<std::size_t> template_of_group_;
    bool ready_ = false;
    bool car_shading_ = false;
    bool prepare_templates(std::string& error);
    bool prepare_frame_cache(std::string& error);
    bool select_track(const TrackCullReport& visible, RaceGpuFrame& out,
                      std::size_t& entries, std::string& error);
    const UploadTable& resource_table() const override { return combined_uploads_; }
    std::size_t selected_draw_count() const override { return sky_draws_ + selection_.size() + car_templates_.owning_draws().size(); }
    RecordedDrawView selected_draw(std::size_t i) const override;
    std::uint64_t command_generation() const override { return generation_; }
    std::size_t static_track_draw_count() const override { return sky_draws_ + selection_.size(); }
    std::size_t static_track_instance_count() const override { return track_placements_.size(); }
    const DrawInstanceUniform& static_track_instance(std::size_t i) const override { return track_placements_.at(i); }
    std::size_t static_track_definition_count() const override { return track_templates_.owning_draws().size(); }
    const RecordedDraw& static_track_definition(std::size_t i) const override { return track_templates_.owning_draws().at(i); }
    PreparedTrackCull culler_;
    UploadTable combined_uploads_;
    RecordedCommands car_templates_;
    std::vector<std::size_t> mesh_first_, section_first_;
    std::vector<std::uint32_t> mesh_groups_;
    struct SelectedTrackDraw { std::uint32_t instance, draw; std::size_t ordinal; };
    struct RetailGroup { std::size_t bucket = 0; unsigned char layer = 0; };
    std::vector<SelectedTrackDraw> selection_;
    std::vector<RetailGroup> retail_groups_;
    std::vector<std::size_t> bucket_first_;
    std::vector<DrawInstanceUniform> track_placements_;
    std::uint64_t generation_ = 0;
    PreparedCarScene car_;
    std::vector<PreparedInstance> base_;
    std::size_t skin_source_ = kNoHandle;
    std::uint64_t car_bytes_ = 0;
};
} // namespace nfsu
