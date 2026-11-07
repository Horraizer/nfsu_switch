#pragma once
#include "nfsu/race_session.h"
#include "nfsu/car_assets.h"
#include "nfsu/draw_commands.h"
#include <array>
#include <filesystem>

namespace nfsu {
struct WheelSkidInput {
    VehicleVec3 point,normal{0,0,1},velocity,side{0,1,0};
    float width=0,friction_usage=0,load_kn=0;
    std::uint32_t surface=0;
    bool contact=false,sliding=false,inhibited=false;
};
struct WheelSkidSample {
    VehicleVec3 point,cross_section;
    float opacity=0;
    unsigned texture=0;
};
// Native587E90 gates/width/opacity, with the supplied local ground plane in
// place of its two world queries. False breaks the current ribbon.
bool original_wheel_skid(const WheelSkidInput&,WheelSkidSample&) noexcept;

// Original textures and emission rules, bounded output-only ribbon adapter.
// Player-wheel output, three material batches, no per-mark draw calls or
// physics queries. Remote selector3 wheel-effect telemetry is not inferred.
class RaceSkidMarks {
public:
    static constexpr unsigned kSegments=512;
    bool load(const std::filesystem::path&,const CarAssets&,std::string&);
    void reset() noexcept;
    void advance(const RaceSession&) noexcept;
    bool append(RecordedCommands&,std::string&);
    void place(RecordedCommands&,std::size_t first);
    std::size_t segment_count() const noexcept {return count_;}
    std::size_t resident_bytes() const noexcept;
    // Explicit adapter seam also used by the native-emission regression.
    void observe(unsigned wheel,const WheelSkidSample* sample) noexcept;
private:
    struct Segment {WheelSkidSample a,b;};
    struct Link {WheelSkidSample last;std::size_t slot=0;bool active=false,has_slot=false;};
    std::array<Segment,kSegments> segments_{};
    std::array<Link,4> links_{};
    std::array<float,4> widths_{};
    std::array<RenderTexture,3> textures_;
    std::array<MeshVertex,kSegments*4*3> vertices_{};
    std::array<std::uint16_t,kSegments*6> indices_{};
    std::array<std::size_t,3> visible_{};
    std::uint64_t tick_=0;
    std::size_t next_=0,count_=0;
    bool have_tick_=false,dirty_=true;
    void rebuild() noexcept;
};
}
