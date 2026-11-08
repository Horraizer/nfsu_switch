#pragma once
#include "nfsu/draw_commands.h"
#include "nfsu/race_session.h"
#include <memory>

namespace nfsu {
// Single-player, one-car scene from INGAMEB playlist list 9 (47BC7D).
// Deterministic port choice; retail can choose any of 2331..2334.
inline constexpr unsigned kFirstDriftStartScene=2333;
// Authored race start scene. Retail payloads are decoded at load, never embedded.
// Fractional skeletal poses use fenced streamed vertices; scene end retires the crowd after GO.
bool load_drift_start_pose(const std::filesystem::path&,const VehicleTrackCourse&,VehiclePose&,std::string&,const std::string& region="L1RB");
// Single-player road playlists in original47BC63. Variant is an index into
// retail data, not a scene ID. The caller supplies a presentation RNG value.
unsigned road_start_playlist(unsigned racers,unsigned grid);
bool select_road_start_scene(FileSystemService&,const std::filesystem::path&,
                            unsigned racers,unsigned grid,unsigned variant,unsigned&,std::string&);
struct RaceStartClock { unsigned release=0,go=0,end=0; };
class StartCrowd {
public:
    StartCrowd();
    ~StartCrowd();
    bool load(FileSystemService&, const std::filesystem::path&, std::string&, unsigned scene=2338, unsigned track=1003, const std::string& region="L1RA");
    RaceStartClock clock() const;
    bool load_camera(FileSystemService&,const std::filesystem::path&,unsigned scene,
                     const std::string& region,RaceStartClock&,std::string&);
    const RecordedCommands& commands() const;
    void rebase(std::size_t);
    void frame(const RaceSession&);
    bool car(const RaceSession&,VehicleState&,CarAnimationState* animation=nullptr) const;
    bool camera(const RaceSession&, OrbitCamera&,unsigned width=1280,unsigned height=720) const;
    bool render(const RenderOptions&, Frame&, RenderStats&, std::string&) const;
    std::size_t draw_count() const;
    std::size_t actor_count() const;
    std::uint64_t bytes() const;
private:
    struct Data;
    std::unique_ptr<Data> data_;
};
}
