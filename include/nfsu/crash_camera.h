#pragma once
#include "nfsu/chase_camera.h"
#include "nfsu/ai_road.h"
namespace nfsu {
// Original4AA200 profile+8D, copied to78A3FA by4B47A0.
constexpr unsigned kCrashCameraDefaultIntensity=128;
class CrashCameraWorld {
public:
    virtual ~CrashCameraWorld()=default;
    // Original440A40 first admits terrain below query.z+1.
    virtual bool ground(const AiDriverPoint&,float&) const=0;
};
class CrashCameraRoadSource {
public:
    virtual ~CrashCameraRoadSource()=default;
    virtual bool prepare(const VehicleState&,float,AiRoadChain&,int&) const=0;
    virtual bool progress_at(const AiDriverPoint&,float&) const=0;
};
//457300/457180: the sight line must cross every intervening road strip.
bool crash_camera_road_visible(const AiRoadChain&,const AiDriverPoint&,const AiDriverPoint&,
                               unsigned first,unsigned last) noexcept;
struct CrashCameraShot {
    AiDriverPoint eye{},alternate{},end_point{};
    float start_progress=0,end_progress=0;
    float side=.4F;
};
// Original42C4B0 and42C6D0 cases15/16 over an already selected driver road.
// Missing road/terrain or both obstructed candidates reject, never invent an eye.
bool crash_camera_road_shot(const AiRoadChain&,int category,unsigned track,
    const VehicleState&,const CrashCameraWorld&,CrashCameraShot&) noexcept;
// Specialized485C00: case15/16 have stationary eye tracks and zero roll tracks.
// Screen composition offsets are .2 vertically and +/-.4 horizontally.
bool crash_camera_view(const CrashCameraShot&,unsigned event,const VehicleState&,
                       float progress,const ChaseCameraView& normal,ChaseCameraView&) noexcept;
struct CrashCameraFlight {float height=0,duration=0;};
bool crash_camera_predict(const VehicleState&,const VehicleHandling&,float horizon,
                           const CrashCameraWorld&,CrashCameraFlight&,float wheel_phase=0) noexcept;
class RaceCrashCamera {
public:
    void configure(unsigned track,unsigned intensity) noexcept;
    void reset() noexcept;
    unsigned event() const noexcept {return event_;}
    float scale(float real_dt) const noexcept;
    void begin_frame(float real_dt) noexcept;
    void contact(float magnitude,bool entering,std::int32_t tick,bool allowed,
                 const VehicleState&,float progress,const CrashCameraRoadSource&,
                 const CrashCameraWorld&);
    bool update(std::int32_t tick,std::uint64_t frame,bool allowed,const VehicleState&,
                const VehicleHandling&,float wheel_phase,float progress,const CrashCameraRoadSource&,
                const CrashCameraWorld&,const ChaseCameraView&,ChaseCameraView&);
    std::uint64_t activations=0,returns=0;
private:
    bool request(std::int32_t tick,float progress,const VehicleState&,
                  const CrashCameraRoadSource&,const CrashCameraWorld&);
    void stop() noexcept;
    unsigned track_=0,intensity_=0,event_=0;
    std::int32_t crash_tick_=0,last_[2]{};
    bool crash_valid_=false,last_valid_[2]{};
    float remaining_=0,flight_deadline_=0;
    CrashCameraShot shot_{};
};
} // namespace nfsu
