#pragma once
#include "nfsu/race_run.h"
#include "nfsu/stock_frame.h"

namespace nfsu {
// One combined command stream: track then car, one camera and one depth
// buffer. Scene payloads stay stable until the renderer closes.
bool combine_race_commands(RecordedCommands track, const RecordedCommands& car,
                           RecordedCommands& out, std::string& error);
struct RaceGpuFrame {
    RecordedCommands commands;
    std::size_t track_draws = 0, car_draws = 0, visible = 0;
    std::uint64_t triangles = 0, unplaced = 0;
    double far_plane = 0;
};
class RaceGpuScene {
public:
    TrackSlice track;
    bool load(FileSystemService& files, const std::filesystem::path& root,
              std::string& error, AssetCheckObserver* observer = nullptr);
    bool frame(const RaceSession& session, RaceGpuFrame& out, std::string& error);
    std::uint64_t car_bytes() const { return car_bytes_; }
private:
    PreparedCarScene car_;
    std::vector<PreparedInstance> base_;
    std::size_t skin_source_ = kNoHandle;
    std::uint64_t car_bytes_ = 0;
};
} // namespace nfsu
