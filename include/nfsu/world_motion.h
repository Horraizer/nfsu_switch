#pragma once
#include "nfsu/eagl_animation.h"
#include "nfsu/platform.h"
#include <filesystem>

namespace nfsu {
//47B290 animation clock; endpoint units are samples, not seconds.
struct WorldMotionClock {
    std::uint32_t flags=0;
    float rate=1,end=0,phase=0,hold=0,hold_elapsed=0;
};
std::uint32_t advance_world_motion_clock(WorldMotionClock&,float seconds) noexcept;
//4281E0 prediction fields belong to AnimationMover, not its phase clock.
struct WorldTrainPrediction {bool ready=false;float delay=0,crossing_distance=0;};
float world_train_arrival(float distance,float speed,float acceleration) noexcept;
float world_train_lead(int lane) noexcept;
// Controller4EC record, populated by462E60 from an instantiated animation mover.
struct WorldObstacleBinding {
    unsigned object=~0U;float distance=0,enter=0,leave=0;
    bool bound()const noexcept{return object!=~0U;}
};
struct WorldObstacleStatus {bool available=false,passed=true,clear=true;};
struct WorldObstacleWindow {
    bool bound=false,head_present=false,tail_present=false;
    float enter=0,leave=0,delay=0;
    WorldMotionClock head,tail;
};
//454A20/579FF0/57A020; source frame units and strict interval boundaries.
bool world_obstacle_clear(const WorldObstacleWindow&,float distance,float speed,float acceleration) noexcept;
float world_obstacle_distance(float target,float current,float length,bool circuit) noexcept;
struct WorldMotionClip {
    std::uint32_t hash=0;
    std::vector<eagl::Pose> frames;
};
bool decode_world_motion_clips(const eagl::Bytes&,const std::vector<std::uint32_t>&,
    std::vector<WorldMotionClip>&,std::string&);
bool load_world_motion_clips(FileSystemService&,const std::filesystem::path&,
    const std::vector<std::uint32_t>& wanted,std::vector<WorldMotionClip>&,std::string&);
// Original DeltaF3 linear and DeltaQ normalized-component interpolation.
bool sample_world_motion_clip(const WorldMotionClip&,float frame,eagl::Pose&) noexcept;
std::array<float,16> world_motion_pose_matrix(const eagl::Pose&) noexcept;
} // namespace nfsu
