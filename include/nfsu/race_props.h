#pragma once
#include "nfsu/race_contacts.h"
#include "nfsu/prop_settling.h"
#include "nfsu/track_ground.h"
#include <vector>
namespace nfsu {
struct RaceProp {
    unsigned section=0,instance=0;
    std::uint32_t model=0;
    unsigned sound_material=0;
    std::string name;
    VehicleSpec spec;
    VehicleState state;
    RaceContactBox box;
    RaceTrackBodyBounds bounds;
    RaceContactPose history;
    RaceContactPose placement_pose;
    bool placement_ready=false;
    RaceTrackPolygonCache track_cache;
    std::array<float,4> placement_bounds{};
    int proxy_id=0,body_id=0;
    unsigned world_slot=32;
    std::uint32_t registered_tick=0;
    std::vector<std::array<float,3>> points;
    RacePropImpact impact;
    float lifetime=0,age=0,quiet=0,sphere_radius=0;
    bool reclamation_exempt=false;
    std::uint32_t first_activation_tick=0;
    float support_lift=0,render_support_lift=0;
    bool moved=false,awake=false,expired=false,proxy_discarded=false;
    // Fixed-step clock in original 4000 Hz units, independent of speed quietness.
    std::uint32_t last_car_contact=0;
    unsigned contact_count=0;
    std::uint32_t first_contact_tick=0;
    std::vector<PropGroundContact> ground_contacts;
    unsigned motion_state=0;
    bool terrain_contact=false;
};
// 5974FA..5975FF: visual origin is COM minus its rotated offset and lift.
std::array<float,3> race_prop_visual_position(const RaceProp&) noexcept;
// Authored placements and prop integration. The world owns the live contact
// queue. Standalone diagnostic callers retain the vertex support adapter.
class RaceProps {
public:
    bool load(const std::filesystem::path&,std::string&,const std::string& track_name="TRACKB1003.lzc");
    bool advance(const TrackContactWorld&,std::string&,float dt=1.F/60.F);
    bool meet(VehicleState&,const VehicleSpec&,const RaceContactBox&,std::string&,VehiclePropContactFrame* observed=nullptr);
    bool continue_contact(RaceProp&,const TrackContactWorld&,std::string&,double remaining);
    void observe_contact(RaceProp&,const RacePairContact&,VehiclePropContactFrame*,std::uint32_t& audio_order,
        bool car_contact=true);
    const std::vector<RaceProp>& objects() const {return objects_;}
    std::uint32_t sound_materials() const noexcept {
        std::uint32_t mask=0;
        for(const auto& p:objects_)mask|=1U<<(p.sound_material<32?p.sound_material:31);
        return mask;
    }
    std::uint64_t candidates=0,hits=0,steps=0;
private:
    friend class RaceSession;
    friend struct RaceWorldContactProbe;
    void convert_proxy(RaceProp&,const TrackContactWorld&);
    void maintain_pool(const TrackContactWorld&,const VehicleVec3&,std::uint32_t,std::uint64_t);
    bool advance_body(RaceProp&,const TrackContactWorld&,std::string&,float dt);
    bool prepare_frame(const TrackContactWorld&,std::string&,float dt);
    bool refresh_contact(RaceProp&,VehicleState&);
    void restore_proxy(RaceProp&);
    bool queued_contacts_=false;
    bool world_frame_ready_=false;
    std::vector<RaceProp> objects_;
    // Only the32 live bodies own native440A40 caches, not every placement.
    std::array<TrackGroundCache,32> maintenance_ground_{};
    std::vector<std::size_t> proxy_order_;
    float widest_proxy_=0;
    // Sized once at load to the largest authored hull; reused across substeps.
    std::vector<TrackContactSegmentPair> terrain_queries_;
    std::vector<TrackContact> terrain_hits_;
    std::vector<TrackContactStatus> terrain_statuses_;
    std::vector<std::array<float,3>> terrain_arms_;
    bool world_bounds_ready_=false;
    float lowest_world_=0;
    std::uint32_t clock_=0;
};
}
